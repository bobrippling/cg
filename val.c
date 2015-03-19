#include <stdio.h>
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
		case NAME:
			return to & NAMED;
		case NAME_LVAL:
			return to & ADDRESSABLE;
		case ALLOCA:
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

unsigned val_hash(val *v)
{
	unsigned h = v->type;

	switch(v->type){
		case INT:
		case INT_PTR:
			h ^= v->u.i;
			break;
		case NAME_LVAL:
		case NAME:
			h ^= dynmap_strhash(v->u.addr.u.name.spel);
			break;
		case ALLOCA:
			break;
	}

	return h;
}

bool val_op_maybe(enum op op, val *l, val *r, int *res)
{
	int err;

	if(l->type != INT || r->type != INT)
		return false;

	*res = op_exe(op, l->u.i, r->u.i, &err);

	return !err;
}

bool val_op_maybe_val(enum op op, val *l, val *r, val **res)
{
	int i;
	if(!val_op_maybe(op, l, r, &i))
		return false;

	*res = val_new_i(i);
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
			return val_new_ptr_from_int(sym->u.i + num->u.i);

		case ALLOCA:
		{
			val *alloca = val_new(ALLOCA);
			alloca->u.addr.u.alloca.idx = sym->u.addr.u.alloca.idx + num->u.i;
			return alloca;
		}

		case NAME:
		case NAME_LVAL:
			assert(0 && "can't add to name vals");
	}
	assert(0);
}

char *val_str(val *v)
{
	/* XXX: memleak */
	char buf[256];
	switch(v->type){
		case INT:
		case INT_PTR:
			snprintf(buf, sizeof buf, "%d", v->u.i);
			break;
		case NAME:
		case NAME_LVAL:
			snprintf(buf, sizeof buf, "%s", v->u.addr.u.name.spel);
			break;
		case ALLOCA:
			snprintf(buf, sizeof buf, "alloca.%d", v->u.addr.u.alloca.idx);
			break;
	}
	return xstrdup(buf);
}

static val *val_new(enum val_type k)
{
	/* XXX: memleak */
	val *v = xcalloc(1, sizeof *v);
	v->type = k;
	return v;
}

static val *val_name_new_lval_(bool lval)
{
	/* XXX: static */
	static int n;
	char buf[32];

	val *v = val_new(lval ? NAME_LVAL : NAME);

	snprintf(buf, sizeof buf, "tmp.%d", n++);

	v->u.addr.u.name.spel = xstrdup(buf);
	v->u.addr.u.name.reg = -1;

	return v;
}

val *val_name_new(void)
{
	return val_name_new_lval_(false);
}

val *val_name_new_lval(void)
{
	return val_name_new_lval_(true);
}

val *val_new_i(int i)
{
	val *p = val_new(INT);
	p->u.i = i;
	return p;
}

val *val_new_ptr_from_int(int i)
{
	val *p = val_new_i(i);
	p->type = INT_PTR;
	return p;
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
	lval = VAL_NEED(lval, ADDRESSABLE);
	rval = VAL_NEED(rval, LITERAL | NAMED);

	isn_store(blk, rval, lval);
}

val *val_load(block *blk, val *v)
{
	val *named = val_name_new();

	v = VAL_NEED(v, ADDRESSABLE);

	isn_load(blk, named, v);

	return named;
}

val *val_element(block *blk, val *lval, int i, unsigned elemsz)
{
	const unsigned byteidx = i * elemsz;

	val *pre_existing = val_alloca_idx_get(lval, byteidx);
	if(pre_existing)
		return pre_existing;

	val *named = val_name_new_lval();
	val *vidx = val_new_i(byteidx);

	if(blk) /* else noop */
		isn_elem(blk, lval, vidx, named);

	val_alloca_idx_set(lval, byteidx, named);

	return named;
}

val *val_element_noop(val *lval, int i, unsigned elemsz)
{
	return val_element(NULL, lval, i, elemsz);
}

val *val_add(block *blk, val *a, val *b)
{
	val *named = val_name_new();

	isn_op(blk, op_add, a, b, named);

	return named;
}

val *val_equal(block *blk, val *lhs, val *rhs)
{
	val *eq = val_name_new();

	isn_cmp(blk, lhs, rhs, eq);

	return eq;
}

void val_ret(block *blk, val *r)
{
	isn_ret(blk, r);
}
