#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "mem.h"
#include "dynmap.h"

#include "backend.h"
#include "val_internal.h"
#include "isn.h"
#include "isn_internal.h"
#include "isn_struct.h"
#include "block_struct.h"
#include "block_internal.h"
#include "block.h"

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
		case ISN_JMP:
			break;
		case ISN_CALL:
			val_release(isn->u.call.into);
			val_release(isn->u.call.fn);
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
		case ISN_JMP:    return "jmp";
		case ISN_CALL:   return "call";
	}
	return NULL;
}

void isn_load(block *blk, val *to, val *lval)
{
	isn *isn;

	val_retain(lval);
	val_retain(to);

	if(!blk){
		val_release(lval);
		val_release(to);
		return;
	}

	isn = isn_new(ISN_LOAD, blk);

	isn->u.load.lval = lval;
	isn->u.load.to = to;
}

void isn_store(block *blk, val *from, val *lval)
{
	isn *isn;

	val_retain(from);
	val_retain(lval);

	if(!blk){
		val_release(from);
		val_release(lval);
		return;
	}

	isn = isn_new(ISN_STORE, blk);

	isn->u.store.from = from;
	isn->u.store.lval = lval;
}

void isn_op(block *blk, enum op op, val *lhs, val *rhs, val *res)
{
	isn *isn;

	val_retain(lhs);
	val_retain(rhs);
	val_retain(res);

	if(!blk){
		val_release(lhs);
		val_release(rhs);
		val_release(res);
		return;
	}

	isn = isn_new(ISN_OP, blk);
	isn->u.op.op = op;
	isn->u.op.lhs = lhs;
	isn->u.op.rhs = rhs;
	isn->u.op.res = res;
}

void isn_cmp(block *blk, enum op_cmp cmp, val *lhs, val *rhs, val *res)
{
	isn *isn;

	val_retain(lhs);
	val_retain(rhs);
	val_retain(res);

	if(!blk){
		val_release(lhs);
		val_release(rhs);
		val_release(res);
		return;
	}

	isn = isn_new(ISN_CMP, blk);
	isn->u.cmp.cmp = cmp;
	isn->u.cmp.lhs = lhs;
	isn->u.cmp.rhs = rhs;
	isn->u.cmp.res = res;
}

void isn_zext(block *blk, val *from, val *to)
{
	isn *isn;

	val_retain(from);
	val_retain(to);

	if(!blk){
		val_release(from);
		val_release(to);
		return;
	}

	isn = isn_new(ISN_EXT, blk);
	isn->u.ext.from = from;
	isn->u.ext.to = to;
}

void isn_elem(block *blk, val *lval, val *add, val *res)
{
	isn *isn;

	val_retain(lval);
	val_retain(add);
	val_retain(res);

	if(!blk){
		val_release(lval);
		val_release(add);
		val_release(res);
		return;
	}

	isn = isn_new(ISN_ELEM, blk);
	isn->u.elem.lval = lval;
	isn->u.elem.add = add;
	isn->u.elem.res = res;
}

void isn_alloca(block *blk, unsigned sz, val *v)
{
	isn *isn;

	val_retain(v);

	if(!blk){
		val_release(v);
		return;
	}

	isn = isn_new(ISN_ALLOCA, blk);
	isn->u.alloca.sz = sz;
	isn->u.alloca.out = v;
}

void isn_ret(block *blk, val *r)
{
	isn *isn;

	val_retain(r);

	if(!blk){
		val_release(r);
		return;
	}

	isn = isn_new(ISN_RET, blk);
	isn->u.ret = r;
	block_set_type(blk, BLK_EXIT);
}

void isn_call(block *blk, val *into, val *fn)
{
	isn *isn;

	val_retain(into);
	val_retain(fn);

	if(!blk){
		val_release(into);
		val_release(fn);
		return;
	}

	isn = isn_new(ISN_CALL, blk);
	isn->u.call.fn = fn;
	isn->u.call.into = into;
}

void isn_br(block *current, val *cond, block *btrue, block *bfalse)
{
	isn *isn;

	val_retain(cond);

	if(!current){
		val_release(cond);
		return;
	}

	isn = isn_new(ISN_BR, current);
	isn->u.branch.cond = cond;
	isn->u.branch.t = btrue;
	isn->u.branch.f = bfalse;

	block_set_type(current, BLK_BRANCH);

	current->u.branch.cond = val_retain(cond);
	current->u.branch.t = btrue;
	current->u.branch.f = bfalse;
}

void isn_jmp(block *current, block *new)
{
	isn *isn;

	if(!current)
		return;

	isn = isn_new(ISN_JMP, current);
	isn->u.jmp.target = new;

	block_set_type(current, BLK_JMP);

	current->u.jmp.target = new; /* weak ref */
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

		case ISN_JMP:
			break;

		case ISN_BR:
			fn(current->u.branch.cond, current, ctx);
			break;

		case ISN_CALL:
			fn(current->u.call.into, current, ctx);
			fn(current->u.call.fn, current, ctx);
			break;
	}
}

static void isn_dump1(isn *i)
{
	switch(i->type){
		case ISN_STORE:
		{
			printf("\tstore.%u %s, %s\n",
					val_size(i->u.store.from, 0),
					val_str_rn(0, i->u.store.lval),
					val_str_rn(1, i->u.store.from));
			break;
		}

		case ISN_LOAD:
		{
			printf("\t%s = load.%u %s\n",
					val_str_rn(0, i->u.load.to),
					val_size(i->u.load.to, 0),
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
					val_size(i->u.cmp.lhs, 0),
					val_str_rn(1, i->u.cmp.lhs),
					val_str_rn(2, i->u.cmp.rhs));
			break;
		}

		case ISN_OP:
		{
			printf("\t%s = %s.%u %s, %s\n",
					val_str_rn(0, i->u.op.res),
					op_to_str(i->u.op.op),
					val_size(i->u.op.lhs, 0),
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
					val_size(i->u.copy.to, 0),
					val_str_rn(1, i->u.copy.from));
			break;
		}

		case ISN_RET:
		{
			printf("\tret.%u %s\n",
					val_size(i->u.ret, 0),
					val_str(i->u.ret));
			break;
		}

		case ISN_JMP:
		{
			printf("\tjmp %s\n", i->u.jmp.target->lbl);
			break;
		}

		case ISN_CALL:
		{
			if(i->u.call.into){
				printf("\t%s = call %s\n",
						val_str_rn(0, i->u.call.into),
						val_str_rn(1, i->u.call.fn));
			}else{
				printf("\tcall %s\n", val_str(i->u.call.fn));
			}
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

#include "dynmap.h"
#include "val_struct.h"
static void get_val(val *v, isn *isn, void *ctx)
{
	(void)isn;
	if(v->type != NAME)
		return;

	dynmap_set(val *, long, ctx, v, 0l);
}

#define SHOW_LIFE 1
void isn_dump(isn *const head, block *blk)
{
	size_t idx = 0;
	isn *i;

	for(i = head; i; i = i->next){
		if(i->skip)
			continue;

		if(SHOW_LIFE)
			printf("[%zu] ", idx++);

		isn_dump1(i);
	}

	if(SHOW_LIFE){
		dynmap *vals = dynmap_new(val *, 0, val_hash);

		for(i = head; i; i = i->next){
			if(i->skip)
				continue;

			isn_on_vals(i, get_val, vals);
		}

		val *v;
		for(idx = 0; (v = dynmap_key(val *, vals, idx)); idx++){
			struct lifetime *lt = dynmap_get(
					val *, struct lifetime *,
					block_lifetime_map(blk),
					v);

			assert(lt && "val doesn't have a lifetime");

			printf("[-] %s: %u - %u. inter-block = %d\n",
					val_str(v),
					lt->start,
					lt->end,
					v->live_across_blocks);
		}

		dynmap_free(vals);
	}
}
