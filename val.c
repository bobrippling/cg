#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "mem.h"
#include "dynmap.h"

#include "val.h"
#include "val_internal.h"
#include "val_struct.h"

#include "variable.h"
#include "isn.h"
#include "op.h"
#include "global.h"
#include "type.h"

#if 0
static val *val_new(enum val_kind k);

static bool val_in(val *v, enum val_to to)
{
	switch(v->type){
		case INT:
			return to & LITERAL;
		case INT_PTR:
			return to & (LITERAL | ADDRESSABLE);
		case ARG:
		case NAME:
			return to & NAMED;
		case ALLOCA:
			return to & ADDRESSABLE;
		case LBL:
			return to & ADDRESSABLE;
	}
	assert(0);
}

val *val_need(val *v, enum val_to to, const char *from)
{
	if(val_in(v, to))
		return v;

	fprintf(stderr, "%s: val_need(): don't have 0x%x\n", from, to);
	assert(0);
}

int val_size(val *v, unsigned ptrsz)
{
	switch(v->type){
		case INT_PTR:
		case ALLOCA:
		case LBL:
			return ptrsz;
		case INT:
			return v->u.i.val_size;
		case ARG:
			return v->u.arg.val_size;
		case NAME:
			return v->u.addr.u.name.val_size;
	}
	assert(0);
}
#endif

bool val_is_mem(val *v)
{
	struct name_loc *loc = val_location(v);

	if(v->kind == GLOBAL){
		assert(!loc);
		return true;
	}

	if(loc)
		return loc->where == NAME_SPILT;

	return false;
}

bool val_is_int(val *v, size_t *const out)
{
	if(v->kind != LITERAL)
		return false;

	*out = v->u.i;
	return true;
}

unsigned val_hash(val *v)
{
	unsigned h = v->kind;
	const char *spel = NULL;

	switch(v->kind){
		case LITERAL:
			h ^= v->u.i;
			break;

		case GLOBAL:
		{
			spel = global_name(v->u.global);
			break;
		}

		case ARGUMENT:
		{
			h ^= v->u.argument.idx;
			break;
		}

		case FROM_ISN:
			break;

		case BACKEND_TEMP:
			break;
	}

	if(spel)
		h ^= dynmap_strhash(spel);

	return h;
}

struct name_loc *val_location(val *v)
{
	switch(v->kind){
		case FROM_ISN:
			return &v->u.local.loc;

		case ARGUMENT:
			return function_arg_loc(v->u.argument.func, v->u.argument.idx);

		case BACKEND_TEMP:
			return &v->u.temp_loc;

		case GLOBAL:
		case LITERAL:
			break;
	}
	return NULL;
}

#if 0
static bool val_both_ints(val *l, val *r)
{
	if(l->kind != INT || r->kind != INT)
		return false;

	assert(l->u.i.val_size == r->u.i.val_size);
	return true;
}

bool val_op_maybe(enum op op, val *l, val *r, int *res)
{
	int err;

	if(!val_both_ints(l, r))
		return false;

	*res = op_exe(op, l->u.i.i, r->u.i.i, &err);

	return !err;
}

bool val_cmp_maybe(enum op_cmp cmp, val *l, val *r, int *res)
{
	if(!val_both_ints(l, r))
		return false;

	*res = op_cmp_exe(cmp, l->u.i.i, r->u.i.i);
	return true;
}

val *val_op_symbolic(enum op op, val *l, val *r)
{
	val *res;

	if(!val_op_maybe_val(op, l, r, &res))
		return res; /* fully integral */

	/* one side must be numeric */
	val *num = NULL;
	if(l->kind == INT)
		num = l;
	if(r->kind == INT)
		num = r;
	if(!num)
		assert(0 && "symbolic op needs an int");

	val *sym = (num == l ? r : l);
	switch(sym->kind){
		case INT:
			assert(0 && "unreachable");

		case INT_PTR:
			return val_new_ptr_from_int(sym->u.i.i + num->u.i.i);

		case ALLOCA:
		{
			val *alloca = val_new(ALLOCA);
			alloca->u.addr.u.alloca.idx = sym->u.addr.u.alloca.idx + num->u.i.i;
			return alloca;
		}

		case LBL:
		case NAME:
		case ARG:
			assert(0 && "can't add to name vals");
	}
	assert(0);
}
#endif

char *val_str_r(char buf[32], val *v)
{
	switch(v->kind){
		case LITERAL:
			if(type_is_void(v->ty))
				snprintf(buf, VAL_STR_SZ, "void");
			else
				snprintf(buf, VAL_STR_SZ, "%s %d", type_to_str(v->ty), v->u.i);
			break;
		case GLOBAL:
			snprintf(buf, VAL_STR_SZ, "$%s", global_name(v->u.global));
			break;
		case ARGUMENT:
			snprintf(buf, VAL_STR_SZ, "$%s", v->u.argument.name);
			break;
		case FROM_ISN:
			snprintf(buf, VAL_STR_SZ, "$%s", v->u.local.name);
			break;
		case BACKEND_TEMP:
			snprintf(buf, VAL_STR_SZ, "<temp %p>", v);
			break;
	}
	return buf;
}

char *val_str(val *v)
{
	static char buf[VAL_STR_SZ];
	return val_str_r(buf, v);
}

char *val_str_rn(unsigned buf_n, val *v)
{
	static char buf[3][VAL_STR_SZ];
	assert(buf_n < 3);
	return val_str_r(buf[buf_n], v);
}

static val *val_new(enum val_kind k, struct type *ty)
{
	val *v = xcalloc(1, sizeof *v);
	/* v->retains begins at zero - isns initially retain them */
	v->kind = k;
	v->ty = ty;
	return v;
}

val *val_retain(val *v)
{
	/* valid for v->retains to be 0 */
	v->retains++;
	return v;
}

static void val_free(val *v)
{
	free(v);
}

void val_release(val *v)
{
	assert(v->retains > 0 && "unretained val");

	v->retains--;

	if(v->retains == 0)
		val_free(v);
}

#if 0
val *val_name_new(unsigned sz, char *ident)
{
	val *v = val_new(NAME);

	if(!ident){
		/* XXX: static */
		static int n;
		char buf[32];
		snprintf(buf, sizeof buf, "tmp_%d", n++);
		ident = xstrdup(buf);
	}

	v->u.addr.u.name.spel = ident;
	v->u.addr.u.name.loc.where = NAME_IN_REG;
	v->u.addr.u.name.loc.u.reg = -1;
	v->u.addr.u.name.val_size = sz;

	return v;
}
#endif

val *val_new_i(int i, struct type *ty)
{
	val *p = val_new(LITERAL, ty);
	p->u.i = i;
	return p;
}

val *val_new_void(struct uniq_type_list *us)
{
	val *v = val_new(LITERAL, type_get_void(us));
	v->u.i = 0;
	return v;
}

val *val_new_argument(char *name, int idx, struct type *ty, struct function *fn)
{
	val *p = val_new(ARGUMENT, ty);
	p->u.argument.idx = idx;
	p->u.argument.name = name;
	p->u.argument.func = fn;
	return p;
}

val *val_new_global(struct uniq_type_list *us, struct global *glob)
{
	val *p = val_new(GLOBAL, global_type_as_ptr(us, glob));
	p->u.global = glob;
	return p;
}

val *val_new_local(char *name, struct type *ty)
{
	val *p = val_new(FROM_ISN, ty);
	p->u.local.name = name;
	name_loc_init_reg(&p->u.local.loc);
	return p;
}

void val_temporary_init(val *vtmp, type *ty)
{
	assert(ty);

	memset(vtmp, 0, sizeof *vtmp);

	vtmp->kind = BACKEND_TEMP;
	vtmp->ty = ty;
	vtmp->retains = 1;
	name_loc_init_reg(&vtmp->u.temp_loc);
}

val *val_new_temporary(struct type *ty)
{
	val *v = val_new(BACKEND_TEMP, ty);
	val_temporary_init(v, ty);
	return v;
}

struct type *val_type(val *v)
{
	return v->ty;
}

unsigned val_size(val *v)
{
	return type_size(val_type(v));
}
