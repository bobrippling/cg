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
#include "type.h"

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
			val_release(isn->u.elem.index);
			val_release(isn->u.elem.res);
			break;
		case ISN_PTRADD:
			val_release(isn->u.ptradd.lhs);
			val_release(isn->u.ptradd.rhs);
			val_release(isn->u.ptradd.out);
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
		case ISN_EXT_TRUNC:
			val_release(isn->u.ext.from);
			val_release(isn->u.ext.to);
			break;
		case ISN_PTR2INT:
		case ISN_INT2PTR:
			val_release(isn->u.ptr2int.from);
			val_release(isn->u.ptr2int.to);
			break;
		case ISN_PTRCAST:
			val_release(isn->u.ptrcast.from);
			val_release(isn->u.ptrcast.to);
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
		{
			size_t i;

			if(isn->u.call.into_or_null)
				val_release(isn->u.call.into_or_null);
			val_release(isn->u.call.fn);

			dynarray_iter(&isn->u.call.args, i){
				val_release(dynarray_ent(&isn->u.call.args, i));
			}
			break;
		}
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
		case ISN_PTRADD: return "ptradd";
		case ISN_OP:     return "op";
		case ISN_CMP:    return "cmp";
		case ISN_COPY:   return "copy";
		case ISN_EXT_TRUNC: return "zext/sext/trunc";
		case ISN_RET:    return "ret";
		case ISN_BR:     return "br";
		case ISN_JMP:    return "jmp";
		case ISN_CALL:   return "call";
		case ISN_PTR2INT:return "ptr2int";
		case ISN_INT2PTR:return "int2ptr";
		case ISN_PTRCAST:return "ptrcast";
	}
	return NULL;
}

void isn_load(block *blk, val *to, val *lval)
{
	isn *isn;

	val_retain(lval);
	val_retain(to);

	assert(type_deref(val_type(lval)) == val_type(to));

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

	assert(type_deref(val_type(lval)) == val_type(from));

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

	assert(type_size(val_type(lhs)) == type_size(val_type(rhs)));
	assert(val_type(lhs) == val_type(res) || val_type(rhs) == val_type(res));

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

	assert(val_type(lhs) == val_type(rhs));
	assert(type_is_primitive(val_type(res), i1));

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

static void isn_ext(block *blk, val *from, val *to, bool sign)
{
	isn *isn;

	val_retain(from);
	val_retain(to);

	assert(type_is_int(val_type(from)));
	assert(type_is_int(val_type(to)));

	if(!blk){
		val_release(from);
		val_release(to);
		return;
	}

	isn = isn_new(ISN_EXT_TRUNC, blk);
	isn->u.ext.from = from;
	isn->u.ext.to = to;
	isn->u.ext.sign = sign;
}

void isn_zext(block *blk, val *from, val *to)
{
	isn_ext(blk, from, to, false);
}

void isn_sext(block *blk, val *from, val *to)
{
	isn_ext(blk, from, to, true);
}

void isn_trunc(block *blk, val *from, val *to)
{
	isn_ext(blk, from, to, /*doesn't matter*/false);
}

static void isn_i2p_p2i(
		block *blk,
		struct val *from, struct val *to,
		enum isn_type kind)
{
	isn *isn;

	val_retain(from);
	val_retain(to);

	if(!blk){
		val_release(from);
		val_release(to);
		return;
	}

	isn = isn_new(kind, blk);
	isn->u.ptr2int.from = from;
	isn->u.ptr2int.to = to;
}

void isn_ptr2int(block *blk, struct val *from, struct val *to)
{
	assert(type_deref(val_type(from)));
	assert(type_is_int(val_type(to)));

	isn_i2p_p2i(blk, from, to, ISN_PTR2INT);
}

void isn_int2ptr(block *blk, struct val *from, struct val *to)
{
	assert(type_is_int(val_type(from)));
	assert(type_deref(val_type(to)));

	isn_i2p_p2i(blk, from, to, ISN_INT2PTR);
}

void isn_ptrcast(block *blk, struct val *from, struct val *to)
{
	assert(type_deref(val_type(from)));
	assert(type_deref(val_type(to)));

	isn_i2p_p2i(blk, from, to, ISN_PTRCAST);
}

void isn_elem(block *blk, val *lval, val *index, val *res)
{
	isn *isn;

	val_retain(lval);
	val_retain(index);
	val_retain(res);

	if(!blk){
		val_release(lval);
		val_release(index);
		val_release(res);
		return;
	}

	isn = isn_new(ISN_ELEM, blk);
	isn->u.elem.lval = lval;
	isn->u.elem.index = index;
	isn->u.elem.res = res;
}

void isn_ptradd(block *blk, val *lhs, val *rhs, val *out)
{
	isn *isn;

	val_retain(lhs);
	val_retain(rhs);
	val_retain(out);

	if(!blk){
		val_release(lhs);
		val_release(rhs);
		val_release(out);
		return;
	}

	isn = isn_new(ISN_PTRADD, blk);
	isn->u.ptradd.lhs = lhs;
	isn->u.ptradd.rhs = rhs;
	isn->u.ptradd.out = out;
}

void isn_copy(block *blk, val *lval, val *rval)
{
	isn *isn;

	val_retain(lval);
	val_retain(rval);

	if(!blk){
		val_release(lval);
		val_release(rval);
		return;
	}

	isn = isn_new(ISN_COPY, blk);
	isn->u.copy.from = rval;
	isn->u.copy.to = lval;
}

void isn_alloca(block *blk, val *v)
{
	isn *isn;

	assert(type_deref(val_type(v))
			&& "pointer expected for alloca next type");

	val_retain(v);

	if(!blk){
		val_release(v);
		return;
	}

	isn = isn_new(ISN_ALLOCA, blk);
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

void isn_call(block *blk, val *into, val *fn, dynarray *args)
{
	isn *isn;
	size_t i;
	type *fn_ret;
	dynarray *argtys;
	bool variadic;

	if(into)
		val_retain(into);
	val_retain(fn);

	dynarray_iter(args, i){
		val_retain(dynarray_ent(args, i));
	}

	fn_ret = type_func_call(type_deref(val_type(fn)), &argtys, &variadic);

	assert(!into || val_type(into) == fn_ret);

	if(variadic){
		assert(dynarray_count(args) >= dynarray_count(argtys));
	}else{
		assert(dynarray_count(args) == dynarray_count(argtys));
	}
	dynarray_iter(argtys, i){
		assert(val_type(dynarray_ent(args, i)) == dynarray_ent(argtys, i));
	}

	if(!blk){
		if(into)
			val_release(into);
		val_release(fn);

		dynarray_iter(args, i){
			val_release(dynarray_ent(args, i));
		}
		return;
	}

	isn = isn_new(ISN_CALL, blk);
	isn->u.call.fn = fn;
	isn->u.call.into_or_null = into;
	dynarray_move(&isn->u.call.args, args);
}

void isn_br(block *current, val *cond, block *btrue, block *bfalse)
{
	isn *isn;

	val_retain(cond);

	assert(type_is_primitive(val_type(cond), i1));

	if(!current){
		val_release(cond);
		return;
	}

	isn = isn_new(ISN_BR, current);
	isn->u.branch.cond = cond;
	isn->u.branch.t = btrue;
	isn->u.branch.f = bfalse;

	block_set_type(current, BLK_BRANCH);

	block_add_pred(btrue, current);
	block_add_pred(bfalse, current);

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

	block_add_pred(new, current);

	current->u.jmp.target = new; /* weak ref */
}

bool isn_is_noop(isn *isn, struct val **const src, struct val **const dest)
{
	switch(isn->type){
		case ISN_STORE:
		case ISN_LOAD:
		case ISN_ALLOCA:
		case ISN_ELEM:
		case ISN_PTRADD:
		case ISN_OP:
		case ISN_CMP:
		case ISN_COPY:
		case ISN_EXT_TRUNC:
		case ISN_RET:
		case ISN_JMP:
		case ISN_BR:
		case ISN_CALL:
			break;

		case ISN_PTR2INT:
		case ISN_INT2PTR:
			*src = isn->u.ptr2int.from;
			*dest = isn->u.ptr2int.to;
			return val_size(*src) == val_size(*dest);

		case ISN_PTRCAST:
			/* assumes all pointers are the same size and representation */
			*src = isn->u.ptrcast.from;
			*dest = isn->u.ptrcast.to;
			return true;
	}

	return false;
}

static void isn_on_vals(
		isn *current,
		void fn(val *, isn *, void *),
		void *ctx,
		bool include_dead)
{
	if(!include_dead && current->skip)
		return;

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
			fn(current->u.elem.index, current, ctx);
			break;

		case ISN_PTRADD:
			fn(current->u.ptradd.lhs, current, ctx);
			fn(current->u.ptradd.rhs, current, ctx);
			fn(current->u.ptradd.out, current, ctx);
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

		case ISN_EXT_TRUNC:
			fn(current->u.ext.to, current, ctx);
			fn(current->u.ext.from, current, ctx);
			break;

		case ISN_PTR2INT:
		case ISN_INT2PTR:
			fn(current->u.ptr2int.to, current, ctx);
			fn(current->u.ptr2int.from, current, ctx);
			break;

		case ISN_PTRCAST:
			fn(current->u.ptrcast.to, current, ctx);
			fn(current->u.ptrcast.from, current, ctx);
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
		{
			size_t i;

			if(current->u.call.into_or_null)
				fn(current->u.call.into_or_null, current, ctx);
			fn(current->u.call.fn, current, ctx);

			dynarray_iter(&current->u.call.args, i){
				fn(dynarray_ent(&current->u.call.args, i), current, ctx);
			}
			break;
		}
	}
}

void isn_on_all_vals(isn *current, void fn(val *, isn *, void *), void *ctx)
{
	isn_on_vals(current, fn, ctx, true);
}

void isn_on_live_vals(isn *current, void fn(val *, isn *, void *), void *ctx)
{
	isn_on_vals(current, fn, ctx, false);
}


static void isn_dump1(isn *i)
{
	switch(i->type){
		case ISN_STORE:
		{
			printf("\tstore %s, %s\n",
					val_str_rn(0, i->u.store.lval),
					val_str_rn(1, i->u.store.from));
			break;
		}

		case ISN_LOAD:
		{
			printf("\t%s = load %s\n",
					val_str_rn(0, i->u.load.to),
					val_str_rn(1, i->u.load.lval));

			break;
		}

		case ISN_ALLOCA:
		{
			printf("\t%s = alloca %s\n",
					val_str(i->u.alloca.out),
					type_to_str(type_deref(val_type(i->u.alloca.out))));
			break;
		}

		case ISN_ELEM:
		{
			printf("\t%s = elem %s, %s\n",
					val_str_rn(0, i->u.elem.res),
					val_str_rn(1, i->u.elem.lval),
					val_str_rn(2, i->u.elem.index));
			break;
		}

		case ISN_PTRADD:
		{
			printf("\t%s = ptradd %s, %s\n",
					val_str_rn(0, i->u.ptradd.out),
					val_str_rn(1, i->u.ptradd.lhs),
					val_str_rn(2, i->u.ptradd.rhs));
			break;
		}

		case ISN_CMP:
		{
			printf("\t%s = %s %s, %s\n",
					val_str_rn(0, i->u.cmp.res),
					op_cmp_to_str(i->u.cmp.cmp),
					val_str_rn(1, i->u.cmp.lhs),
					val_str_rn(2, i->u.cmp.rhs));
			break;
		}

		case ISN_OP:
		{
			printf("\t%s = %s %s, %s\n",
					val_str_rn(0, i->u.op.res),
					op_to_str(i->u.op.op),
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

		case ISN_EXT_TRUNC:
		{
			const char *ext_kind;

			if(val_size(i->u.ext.to) > val_size(i->u.ext.from)){
				ext_kind = (i->u.ext.sign ? "sext" : "zext");
			}else{
				ext_kind = "trunc";
			}

			printf("\t%s = %s %s, %s\n",
					val_str_rn(0, i->u.ext.to),
					ext_kind,
					type_to_str(val_type(i->u.ext.to)),
					val_str_rn(1, i->u.ext.from));
			break;
		}

		case ISN_INT2PTR:
		case ISN_PTR2INT:
		{
			printf("\t%s = %s %s, %s\n",
					val_str_rn(0, i->u.ptr2int.to),
					isn_type_to_str(i->type),
					type_to_str(val_type(i->u.ptr2int.to)),
					val_str_rn(1, i->u.ptr2int.from));
			break;
		}

		case ISN_PTRCAST:
		{
			printf("\t%s = ptrcast %s, %s\n",
					val_str_rn(0, i->u.ptrcast.to),
					type_to_str(val_type(i->u.ptrcast.to)),
					val_str_rn(1, i->u.ptrcast.from));
			break;
		}

		case ISN_RET:
		{
			printf("\tret %s\n", val_str(i->u.ret));
			break;
		}

		case ISN_JMP:
		{
			printf("\tjmp $%s\n", i->u.jmp.target->lbl);
			break;
		}

		case ISN_CALL:
		{
			size_t argi;
			const char *sep = "";

			printf("\t");

			if(i->u.call.into_or_null){
				printf("%s = ", val_str(i->u.call.into_or_null));
			}

			printf("call %s(", val_str(i->u.call.fn));

			dynarray_iter(&i->u.call.args, argi){
				val *arg = dynarray_ent(&i->u.call.args, argi);

				printf("%s%s", sep, val_str(arg));

				sep = ", ";
			}

			printf(")\n");
			break;
		}

		case ISN_BR:
		{
			printf("\tbr %s, $%s, $%s\n",
					val_str(i->u.branch.cond),
					i->u.branch.t->lbl,
					i->u.branch.f->lbl);
			break;
		}
	}
}

#include "dynmap.h"
#include "val_struct.h"
static void get_named_val(val *v, isn *isn, void *ctx)
{
	(void)isn;

	switch(v->kind){
		case LITERAL:
		case GLOBAL:
		case BACKEND_TEMP:
			return;
		case ALLOCA:
		case ARGUMENT:
		case FROM_ISN:
			break;
	}

	dynmap_set(val *, long, ctx, v, 0l);
}

#define SHOW_LIFE 0
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

		for(i = head; i; i = i->next)
			isn_on_live_vals(i, get_named_val, vals);

		val *v;
		for(idx = 0; (v = dynmap_key(val *, vals, idx)); idx++){
			struct lifetime *lt = dynmap_get(
					val *, struct lifetime *,
					block_lifetime_map(blk),
					v);

			if(lt){
				printf("[-] %s: %u - %u. inter-block = %d\n",
						val_str(v),
						lt->start,
						lt->end,
						v->live_across_blocks
						);
			}else{
				printf("[-] %s: no-ltime inter-block = %d\n",
						val_str(v), v->live_across_blocks);
			}
		}

		dynmap_free(vals);
	}
}
