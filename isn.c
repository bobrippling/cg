#include <stdio.h>
#include <stdlib.h>

#include "mem.h"

#include "backend.h"
#include "val_internal.h"
#include "isn.h"
#include "isn_internal.h"
#include "isn_struct.h"
#include "block_internal.h"
#include "block_struct.h"

static isn *isn_new(enum isn_type t, block *blk)
{
	isn *isn = xcalloc(1, sizeof *isn);

	block_add_isn(blk, isn);

	isn->type = t;
	return isn;
}

static void isn_free_1(isn *isn)
{
	switch(isn->type){
		case ISN_STORE:
			val_release(isn->u.store.lval);
			val_release(isn->u.store.from);
			break;
		case ISN_LOAD:
			val_release(isn->u.load.lval);
			val_release(isn->u.load.to);
			break;
		case ISN_ALLOCA:
			val_release(isn->u.alloca.out);
			break;
		case ISN_ELEM:
			val_release(isn->u.elem.lval);
			val_release(isn->u.elem.add);
			val_release(isn->u.elem.res);
			break;
		case ISN_OP:
			val_release(isn->u.op.lhs);
			val_release(isn->u.op.rhs);
			val_release(isn->u.op.res);
			break;
		case ISN_CMP:
			val_release(isn->u.cmp.lhs);
			val_release(isn->u.cmp.rhs);
			val_release(isn->u.cmp.res);
			break;
		case ISN_COPY:
			val_release(isn->u.copy.from);
			val_release(isn->u.copy.to);
			break;
		case ISN_EXT:
			val_release(isn->u.ext.from);
			val_release(isn->u.ext.to);
			break;
		case ISN_RET:
			val_release(isn->u.ret);
			break;
		case ISN_BR:
			val_release(isn->u.branch.cond);
			break;
	}

	free(isn);
}

void isn_free_r(isn *head)
{
	isn *i, *next;
	for(i = head; i; i = next){
		next = i->next;
		isn_free_1(i);
	}
}

const char *isn_type_to_str(enum isn_type t)
{
	switch(t){
		case ISN_STORE:  return "store";
		case ISN_LOAD:   return "load";
		case ISN_ALLOCA: return "alloca";
		case ISN_ELEM:   return "elem";
		case ISN_OP:     return "op";
		case ISN_CMP:    return "cmp";
		case ISN_COPY:   return "copy";
		case ISN_EXT:    return "ext";
		case ISN_RET:    return "ret";
		case ISN_BR:     return "br";
	}
	return NULL;
}

void isn_load(block *blk, val *to, val *lval)
{
	isn *isn = isn_new(ISN_LOAD, blk);

	isn->u.load.lval = val_retain(lval);
	isn->u.load.to = val_retain(to);
}

void isn_store(block *blk, val *from, val *lval)
{
	isn *isn = isn_new(ISN_STORE, blk);

	isn->u.store.lval = val_retain(lval);
	isn->u.store.from = val_retain(from);
}

void isn_op(block *blk, enum op op, val *lhs, val *rhs, val *res)
{
	isn *isn = isn_new(ISN_OP, blk);
	isn->u.op.op = op;
	isn->u.op.lhs = val_retain(lhs);
	isn->u.op.rhs = val_retain(rhs);
	isn->u.op.res = val_retain(res);
}

void isn_cmp(block *blk, enum op_cmp cmp, val *lhs, val *rhs, val *res)
{
	isn *isn = isn_new(ISN_CMP, blk);
	isn->u.cmp.cmp = cmp;
	isn->u.cmp.lhs = val_retain(lhs);
	isn->u.cmp.rhs = val_retain(rhs);
	isn->u.cmp.res = val_retain(res);
}

void isn_zext(block *blk, val *from, val *to)
{
	isn *isn = isn_new(ISN_EXT, blk);
	isn->u.ext.from = val_retain(from);
	isn->u.ext.to = val_retain(to);
}

void isn_elem(block *blk, val *lval, val *add, val *res)
{
	isn *isn = isn_new(ISN_ELEM, blk);
	isn->u.elem.lval = val_retain(lval);
	isn->u.elem.add = val_retain(add);
	isn->u.elem.res = val_retain(res);
}

void isn_alloca(block *blk, unsigned sz, val *v)
{
	isn *isn = isn_new(ISN_ALLOCA, blk);
	isn->u.alloca.sz = sz;
	isn->u.alloca.out = val_retain(v);
}

void isn_ret(block *blk, val *r)
{
	isn *isn = isn_new(ISN_RET, blk);
	isn->u.ret = val_retain(r);
	block_set_type(blk, BLK_EXIT);
}

void isn_br(block *current, val *cond, block *btrue, block *bfalse)
{
	isn *isn = isn_new(ISN_BR, current);
	isn->u.branch.cond = val_retain(cond);
	isn->u.branch.t = btrue;
	isn->u.branch.f = bfalse;

	block_set_type(current, BLK_BRANCH);

	current->u.branch.cond = cond; /* weak ref */
	current->u.branch.t = btrue;
	current->u.branch.f = bfalse;
}

void isn_on_vals(isn *current, void fn(val *, isn *, void *), void *ctx)
{
	switch(current->type){
		case ISN_STORE:
			fn(current->u.store.lval, current, ctx);
			fn(current->u.store.from, current, ctx);
			break;

		case ISN_LOAD:
			fn(current->u.load.to, current, ctx);
			fn(current->u.load.lval, current, ctx);
			break;

		case ISN_ALLOCA:
			fn(current->u.alloca.out, current, ctx);
			break;

		case ISN_ELEM:
			fn(current->u.elem.res, current, ctx);
			fn(current->u.elem.lval, current, ctx);
			fn(current->u.elem.add, current, ctx);
			break;

		case ISN_OP:
			fn(current->u.op.res, current, ctx);
			fn(current->u.op.lhs, current, ctx);
			fn(current->u.op.rhs, current, ctx);
			break;

		case ISN_CMP:
			fn(current->u.cmp.res, current, ctx);
			fn(current->u.cmp.lhs, current, ctx);
			fn(current->u.cmp.rhs, current, ctx);
			break;

		case ISN_COPY:
			fn(current->u.copy.to, current, ctx);
			fn(current->u.copy.from, current, ctx);
			break;

		case ISN_EXT:
			fn(current->u.ext.to, current, ctx);
			fn(current->u.ext.from, current, ctx);
			break;

		case ISN_RET:
			fn(current->u.ret, current, ctx);
			break;

		case ISN_BR:
			fn(current->u.branch.cond, current, ctx);
			break;
	}
}

static void isn_dump1(isn *i)
{
	switch(i->type){
		case ISN_STORE:
		{
			printf("\tstore.%u %s, %s\n",
					val_size(i->u.store.from),
					val_str_rn(0, i->u.store.lval),
					val_str_rn(1, i->u.store.from));
			break;
		}

		case ISN_LOAD:
		{
			printf("\t%s = load.%u %s\n",
					val_str_rn(0, i->u.load.to),
					val_size(i->u.load.to),
					val_str_rn(1, i->u.load.lval));

			break;
		}

		case ISN_ALLOCA:
		{
			printf("\t%s = alloca %u\n",
					val_str(i->u.alloca.out),
					i->u.alloca.sz);
			break;
		}

		case ISN_ELEM:
		{
			printf("\t%s = elem %s, %s\n",
					val_str_rn(0, i->u.elem.res),
					val_str_rn(1, i->u.elem.lval),
					val_str_rn(2, i->u.elem.add));
			break;
		}

		case ISN_CMP:
		{
			printf("\t%s = %s.%u %s, %s\n",
					val_str_rn(0, i->u.cmp.res),
					op_cmp_to_str(i->u.cmp.cmp),
					val_size(i->u.cmp.lhs),
					val_str_rn(1, i->u.cmp.lhs),
					val_str_rn(2, i->u.cmp.rhs));
			break;
		}

		case ISN_OP:
		{
			printf("\t%s = %s.%u %s, %s\n",
					val_str_rn(0, i->u.op.res),
					op_to_str(i->u.op.op),
					val_size(i->u.op.lhs),
					val_str_rn(1, i->u.op.lhs),
					val_str_rn(2, i->u.op.rhs));
			break;
		}

		case ISN_COPY:
		{
			printf("\t%s = %s\n",
					val_str_rn(0, i->u.copy.to),
					val_str_rn(1, i->u.copy.from));
			break;
		}

		case ISN_EXT:
		{
			printf("\t%s = zext %u, %s\n",
					val_str_rn(0, i->u.copy.to),
					val_size(i->u.copy.to),
					val_str_rn(1, i->u.copy.from));
			break;
		}

		case ISN_RET:
		{
			printf("\tret.%u %s\n",
					val_size(i->u.ret),
					val_str(i->u.ret));
			break;
		}

		case ISN_BR:
		{
			printf("\tbr %s, %s, %s\n",
					val_str(i->u.branch.cond),
					i->u.branch.t->lbl,
					i->u.branch.f->lbl);
			break;
		}
	}
}

void isn_dump(isn *const head)
{
	isn *i;

	for(i = head; i; i = i->next){
		if(i->skip)
			continue;

		isn_dump1(i);
	}
}
