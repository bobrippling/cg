#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#include "mem.h"
#include "dynmap.h"

#include "val.h"
#include "val_internal.h"
#include "val_struct.h"
#include "val_allocas.h"

#include "isn.h"
#include "op.h"

static val *val_new(enum val_type k);

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

bool val_is_mem(val *v)
{
	switch(v->type){
		case INT_PTR: return true;
		case ALLOCA:  return true;
		case LBL:     return true;
		case INT:     return false;
		case ARG:     return true;
		case NAME:    return v->u.addr.u.name.loc.where == NAME_SPILT;
	}
	assert(0);
}

unsigned val_hash(val *v)
{
	unsigned h = v->type;

	switch(v->type){
		case INT:
		case INT_PTR:
			h ^= v->u.i.i;
			break;
		case NAME:
			h ^= dynmap_strhash(v->u.addr.u.name.spel);
			break;
		case ALLOCA:
			break;
		case LBL:
			h ^= dynmap_strhash(v->u.addr.u.lbl.spel);
			break;
		case ARG:
			h ^= v->u.arg.idx;
			break;
	}

	return h;
}

static bool val_both_ints(val *l, val *r)
{
	if(l->type != INT || r->type != INT)
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

bool val_op_maybe_val(enum op op, val *l, val *r, val **res)
{
	int i;
	if(!val_op_maybe(op, l, r, &i))
		return false;

	assert(l->type == INT);
	*res = val_new_i(i, l->u.i.val_size);
	return true;
}

val *val_op_symbolic(enum op op, val *l, val *r)
{
	val *res;

	if(!val_op_maybe_val(op, l, r, &res))
		return res; /* fully integral */

	/* one side must be numeric */
	val *num = NULL;
	if(l->type == INT)
		num = l;
	if(r->type == INT)
		num = r;
	if(!num)
		assert(0 && "symbolic op needs an int");

	val *sym = (num == l ? r : l);
	switch(sym->type){
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

char *val_str_r(char buf[32], val *v)
{
	switch(v->type){
		case INT:
		case INT_PTR:
			snprintf(buf, VAL_STR_SZ, "%d", v->u.i.i);
			break;
		case NAME:
			snprintf(buf, VAL_STR_SZ, "%s", v->u.addr.u.name.spel);
			break;
		case ALLOCA:
			snprintf(buf, VAL_STR_SZ, "alloca_%d", v->u.addr.u.alloca.idx);
			break;
		case LBL:
			snprintf(buf, VAL_STR_SZ, "%s", v->u.addr.u.lbl.spel);
			break;
		case ARG:
			snprintf(buf, VAL_STR_SZ, "%s", v->u.arg.name);
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

static val *val_new(enum val_type k)
{
	/* XXX: memleak */
	val *v = xcalloc(1, sizeof *v);
	/* v->retains begins at zero - isns initially retain them */
	v->type = k;
	return v;
}

val *val_retain(val *v)
{
	v->retains++;
	return v;
}

static void val_free(val *v)
{
	switch(v->type){
		case NAME:
			free(v->u.addr.u.name.spel);
			break;
		default:
			break;
	}

	free(v);
}

void val_release(val *v)
{
	v->retains--;

	if(v->retains == 0)
		val_free(v);
}

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

val *val_new_i(int i, unsigned sz)
{
	val *p = val_new(INT);
	p->u.i.i = i;
	p->u.i.val_size = sz;
	return p;
}

val *val_new_ptr_from_int(int i)
{
	val *p = val_new_i(i, 0);
	p->type = INT_PTR;
	return p;
}

val *val_new_lbl(char *lbl)
{
	val *v = val_new(LBL);
	v->u.addr.u.lbl.spel = lbl;
	return v;
}

val *val_new_arg(size_t idx, char *name, unsigned size)
{
	val *v = val_new(ARG);
	v->u.arg.idx = idx;
	v->u.arg.name = name;
	v->u.arg.val_size = size;
	return v;
}

val *val_alloca(void)
{
	return val_new(ALLOCA);
}

val *val_make_alloca(block *blk, int n, unsigned elemsz)
{
	/* XXX: static */
	static int idx;
	const unsigned bytesz = n * elemsz;
	val *v = val_alloca();

	isn_alloca(blk, bytesz, v);

	v->u.addr.u.alloca.bytesz = bytesz;
	v->u.addr.u.alloca.idx = idx++;

	return v;
}

void val_store(block *blk, val *rval, val *lval)
{
	isn_store(blk, rval, lval);
}

val *val_load(block *blk, val *v, unsigned size)
{
	val *named = val_name_new(size, NULL);

	isn_load(blk, named, v);

	return named;
}

val *val_element(block *blk, val *lval, int i, unsigned elemsz, char *ident_to)
{
	const unsigned byteidx = i * elemsz;

	val *pre_existing = val_alloca_idx_get(lval, byteidx);
	if(pre_existing)
		return pre_existing;

	val *elem = val_name_new(elemsz, ident_to);

	if(blk){ /* else noop */
		val *vidx = val_new_i(byteidx, 0);
		isn_elem(blk, lval, vidx, elem);
	}

	val_alloca_idx_set(lval, byteidx, elem);

	return elem;
}

val *val_element_noop(val *lval, int i, unsigned elemsz)
{
	return val_element(NULL, lval, i, elemsz, NULL);
}

val *val_add(block *blk, val *a, val *b)
{
	int sz_a = val_size(a, 0);
	int sz_b = val_size(b, 0);
	val *named;

	assert(sz_a != -1 && sz_b != -1);
	assert(sz_a == sz_b);

	named = val_name_new(sz_a, NULL);

	isn_op(blk, op_add, a, b, named);

	return named;
}

val *val_zext(block *blk, val *v, unsigned to)
{
	unsigned sz = val_size(v, 0);
	val *named;

	assert(sz < to);

	named = val_name_new(to, NULL);

	isn_zext(blk, v, named);

	return named;
}

val *val_equal(block *blk, val *lhs, val *rhs)
{
	val *eq = val_name_new(1, NULL);

	isn_cmp(blk, cmp_eq, lhs, rhs, eq);

	return eq;
}

void val_ret(block *blk, val *r)
{
	isn_ret(blk, r);
}
