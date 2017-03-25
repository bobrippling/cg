#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "mem.h"
#include "dynmap.h"

#include "backend.h"
#include "val_internal.h"
#include "isn.h"
#include "isn_struct.h"
#include "block_struct.h"
#include "block_internal.h"
#include "block.h"
#include "type.h"

isn *isn_new(enum isn_type t)
{
	isn *isn = xcalloc(1, sizeof *isn);
	isn->type = t;
	return isn;
}

static bool isn_in_list(isn *list, isn *candidate)
{
	for(; list; list = isn_next(list))
		if(candidate == list)
			return true;

	return false;
}

void isn_insert_before(isn *target, isn *new)
{
	assert(target && "no insert target");
	assert(new && "insert null isn?");

	assert(!new->prev);
	assert(!new->next);

	assert(!isn_in_list(target, new));

	/* link in new: */
	new->prev = target->prev;
	new->next = target;

	if(target->prev)
		target->prev->next = new;
	target->prev = new;
}

void isn_insert_after(isn *target, isn *new)
{
	assert(target && "no insert target");
	assert(new && "insert null isn?");

	assert(!new->prev);
	assert(!new->next);

	assert(!isn_in_list(target, new));

	/* link in new: */
	new->next = target->next;
	new->prev = target;

	if(target->next)
		target->next->prev = new;
	target->next = new;
}

void isns_insert_before(isn *target, isn *list)
{
	isn *head = isn_first(list);
	isn *tail = isn_last(list);

	assert(!head->prev);
	assert(!tail->next);

	head->prev = target->prev;
	if(target->prev)
		target->prev->next = head;

	tail->next = target;
	target->prev = tail;
}

void isns_insert_after(isn *target, isn *list)
{
	isn *head = isn_first(list);
	isn *tail = isn_last(list);

	assert(!head->prev);
	assert(!tail->next);

	tail->next = target->next;
	if(target->next)
		target->next->prev = tail;

	head->prev = target;
	target->next = head;
}

isn *isn_first(isn *i)
{
	while(i->prev)
		i = i->prev;
	return i;
}

isn *isn_last(isn *i)
{
	while(i->next)
		i = i->next;
	return i;
}

isn *isn_next(isn *i)
{
	return i->next;
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
		case ISN_PTRSUB:
			val_release(isn->u.ptraddsub.lhs);
			val_release(isn->u.ptraddsub.rhs);
			val_release(isn->u.ptraddsub.out);
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

			val_release(isn->u.call.into);
			val_release(isn->u.call.fn);

			dynarray_iter(&isn->u.call.args, i){
				val_release(dynarray_ent(&isn->u.call.args, i));
			}
			dynarray_reset(&isn->u.call.args);
			break;
		}
		case ISN_IMPLICIT_USE:
		{
			size_t i;

			dynarray_iter(&isn->u.implicit_use.vals, i)
				val_release(dynarray_ent(&isn->u.implicit_use.vals, i));
			dynarray_reset(&isn->u.implicit_use.vals);
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
		case ISN_PTRSUB: return "ptrsub";
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
		case ISN_IMPLICIT_USE: return "implicituse";
	}
	return NULL;
}

isn *isn_load(val *to, val *lval)
{
	isn *isn;

	val_retain(lval);
	val_retain(to);

	assert(type_eq(type_deref(val_type(lval)), val_type(to)));

	isn = isn_new(ISN_LOAD);

	isn->u.load.lval = lval;
	isn->u.load.to = to;
	return isn;
}

isn *isn_store(val *from, val *lval)
{
	isn *isn;

	val_retain(from);
	val_retain(lval);

	assert(type_eq(type_deref(val_type(lval)), val_type(from)));

	isn = isn_new(ISN_STORE);

	isn->u.store.from = from;
	isn->u.store.lval = lval;
	return isn;
}

isn *isn_op(enum op op, val *lhs, val *rhs, val *res)
{
	isn *isn;

	val_retain(lhs);
	val_retain(rhs);
	val_retain(res);

	assert(type_size(val_type(lhs)) == type_size(val_type(rhs)));
	assert(type_eq(val_type(lhs), val_type(res)) || type_eq(val_type(rhs), val_type(res)));

	isn = isn_new(ISN_OP);
	isn->u.op.op = op;
	isn->u.op.lhs = lhs;
	isn->u.op.rhs = rhs;
	isn->u.op.res = res;
	return isn;
}

isn *isn_cmp(enum op_cmp cmp, val *lhs, val *rhs, val *res)
{
	isn *isn;

	val_retain(lhs);
	val_retain(rhs);
	val_retain(res);

	assert(type_eq(val_type(lhs), val_type(rhs)));
	assert(type_is_primitive(val_type(res), i1));

	isn = isn_new(ISN_CMP);
	isn->u.cmp.cmp = cmp;
	isn->u.cmp.lhs = lhs;
	isn->u.cmp.rhs = rhs;
	isn->u.cmp.res = res;
	return isn;
}

static isn *isn_ext(val *from, val *to, bool sign)
{
	isn *isn;

	val_retain(from);
	val_retain(to);

	assert(type_is_int(val_type(from)));
	assert(type_is_int(val_type(to)));

	isn = isn_new(ISN_EXT_TRUNC);
	isn->u.ext.from = from;
	isn->u.ext.to = to;
	isn->u.ext.sign = sign;
	return isn;
}

isn *isn_zext(val *from, val *to)
{
	return isn_ext(from, to, false);
}

isn *isn_sext(val *from, val *to)
{
	return isn_ext(from, to, true);
}

isn *isn_trunc(val *from, val *to)
{
	return isn_ext(from, to, /*doesn't matter*/false);
}

static isn *isn_i2p_p2i(
		struct val *from, struct val *to,
		enum isn_type kind)
{
	isn *isn;

	val_retain(from);
	val_retain(to);

	isn = isn_new(kind);
	isn->u.ptr2int.from = from;
	isn->u.ptr2int.to = to;
	return isn;
}

isn *isn_ptr2int(struct val *from, struct val *to)
{
	assert(type_deref(val_type(from)));
	assert(type_is_int(val_type(to)));

	return isn_i2p_p2i(from, to, ISN_PTR2INT);
}

isn *isn_int2ptr(struct val *from, struct val *to)
{
	assert(type_is_int(val_type(from)));
	assert(type_deref(val_type(to)));

	return isn_i2p_p2i(from, to, ISN_INT2PTR);
}

isn *isn_ptrcast(struct val *from, struct val *to)
{
	assert(type_deref(val_type(from)));
	assert(type_deref(val_type(to)));

	return isn_i2p_p2i(from, to, ISN_PTRCAST);
}

isn *isn_elem(val *lval, val *index, val *res)
{
	isn *isn;

	val_retain(lval);
	val_retain(index);
	val_retain(res);

	isn = isn_new(ISN_ELEM);
	isn->u.elem.lval = lval;
	isn->u.elem.index = index;
	isn->u.elem.res = res;
	return isn;
}

isn *isn_ptradd(val *lhs, val *rhs, val *out)
{
	isn *isn;

	val_retain(lhs);
	val_retain(rhs);
	val_retain(out);

	isn = isn_new(ISN_PTRADD);
	isn->u.ptraddsub.lhs = lhs;
	isn->u.ptraddsub.rhs = rhs;
	isn->u.ptraddsub.out = out;
	return isn;
}

isn *isn_ptrsub(val *lhs, val *rhs, val *out)
{
	isn *isn;

	val_retain(lhs);
	val_retain(rhs);
	val_retain(out);

	isn = isn_new(ISN_PTRSUB);
	isn->u.ptraddsub.lhs = lhs;
	isn->u.ptraddsub.rhs = rhs;
	isn->u.ptraddsub.out = out;
	return isn;
}

isn *isn_copy(val *to, val *from)
{
	isn *isn;

	val_retain(to);
	val_retain(from);

	assert(val_type(to) == val_type(from));

	isn = isn_new(ISN_COPY);
	isn->u.copy.from = from;
	isn->u.copy.to = to;
	return isn;
}

isn *isn_implicit_use()
{
	isn *isn = isn_new(ISN_IMPLICIT_USE);
	dynarray_init(&isn->u.implicit_use.vals);
	return isn;
}

void isn_implicit_use_add(isn *i, val *v)
{
	assert(i->type == ISN_IMPLICIT_USE);
	dynarray_add(&i->u.implicit_use.vals, val_retain(v));
}

isn *isn_alloca(val *v)
{
	isn *isn;

	assert(type_deref(val_type(v))
			&& "pointer expected for alloca next type");

	val_retain(v);

	isn = isn_new(ISN_ALLOCA);
	isn->u.alloca.out = v;
	return isn;
}

isn *isn_ret(val *r)
{
	isn *isn;

	val_retain(r);

	isn = isn_new(ISN_RET);
	isn->u.ret = r;
	return isn;
}

isn *isn_call(val *into, val *fn, dynarray *args)
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

	assert(!into || type_eq(val_type(into), fn_ret));

	if(variadic){
		assert(dynarray_count(args) >= dynarray_count(argtys));
	}else{
		assert(dynarray_count(args) == dynarray_count(argtys));
	}
	dynarray_iter(argtys, i){
		assert(type_eq(val_type(dynarray_ent(args, i)), dynarray_ent(argtys, i)));
	}

	isn = isn_new(ISN_CALL);
	isn->u.call.fn = fn;
	isn->u.call.into = into;
	dynarray_move(&isn->u.call.args, args);
	return isn;
}

isn *isn_br(val *cond, block *btrue, block *bfalse)
{
	isn *isn;

	val_retain(cond);

	assert(type_is_primitive(val_type(cond), i1));

	isn = isn_new(ISN_BR);
	isn->u.branch.cond = cond;
	isn->u.branch.t = btrue;
	isn->u.branch.f = bfalse;

	return isn;
}

isn *isn_jmp(block *new)
{
	isn *isn;

	isn = isn_new(ISN_JMP);
	isn->u.jmp.target = new;

	return isn;
}

bool isn_is_noop(isn *isn, struct val **const src, struct val **const dest)
{
	switch(isn->type){
		case ISN_STORE:
		case ISN_LOAD:
		case ISN_ALLOCA:
		case ISN_ELEM:
		case ISN_PTRADD:
		case ISN_PTRSUB:
		case ISN_OP:
		case ISN_CMP:
		case ISN_COPY:
		case ISN_EXT_TRUNC:
		case ISN_RET:
		case ISN_JMP:
		case ISN_BR:
		case ISN_CALL:
			break;

		case ISN_IMPLICIT_USE:
			return true;

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
		case ISN_PTRSUB:
			fn(current->u.ptraddsub.lhs, current, ctx);
			fn(current->u.ptraddsub.rhs, current, ctx);
			fn(current->u.ptraddsub.out, current, ctx);
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

			fn(current->u.call.into, current, ctx);
			fn(current->u.call.fn, current, ctx);

			dynarray_iter(&current->u.call.args, i){
				fn(dynarray_ent(&current->u.call.args, i), current, ctx);
			}
			break;
		}

		case ISN_IMPLICIT_USE:
		{
			size_t i;

			dynarray_iter(&current->u.implicit_use.vals, i)
				fn(dynarray_ent(&current->u.implicit_use.vals, i), current, ctx);
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
		case ISN_PTRSUB:
		{
			printf("\t%s = ptr%s %s, %s\n",
					val_str_rn(0, i->u.ptraddsub.out),
					i->type == ISN_PTRADD ? "add" : "sub",
					val_str_rn(1, i->u.ptraddsub.lhs),
					val_str_rn(2, i->u.ptraddsub.rhs));
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

			printf("%s = ", val_str(i->u.call.into));

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

		case ISN_IMPLICIT_USE:
		{
			size_t j;
			const char *sep = "";

			printf("\timplicituse ");

			dynarray_iter(&i->u.implicit_use.vals, j){
				printf("%s%s", sep, val_str(dynarray_ent(&i->u.implicit_use.vals, j)));
				sep = ", ";
			}
			printf("\n");
			break;
		}
	}
}

bool isn_call_getfnval_ret_args(
		isn *isn,
		struct val **const pfn,
		struct val **const pret,
		dynarray **const pargs)
{
	if(isn->type != ISN_CALL)
		return false;

	*pfn = isn->u.call.fn;
	*pret = isn->u.call.into;
	*pargs = &isn->u.call.args;

	return true;
}

val *isn_is_ret(isn *i)
{
	if(i->type != ISN_RET)
		return NULL;

	return i->u.ret;
}

#include "dynmap.h"
#include "val_struct.h"
static void get_named_val(val *v, isn *isn, void *ctx)
{
	(void)isn;

	switch(v->kind){
		case LITERAL:
		case GLOBAL:
			return;
		case BACKEND_TEMP:
		case ABI_TEMP:
		case ALLOCA:
		case ARGUMENT:
		case FROM_ISN:
			break;
	}

	dynmap_set(val *, long, ctx, v, 0l);
}

#define SHOW_LIFE 0
#define SHOW_TYPE 0
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

	if(SHOW_LIFE || SHOW_TYPE){
		dynmap *vals = dynmap_new(val *, 0, val_hash);

		for(i = head; i; i = i->next)
			isn_on_live_vals(i, get_named_val, vals);

		val *v;
		for(idx = 0; (v = dynmap_key(val *, vals, idx)); idx++){
			printf("# %s:", val_str(v));

			if(SHOW_TYPE)
				printf(" %s", type_to_str(val_type(v)));

			if(SHOW_LIFE){
				struct lifetime *lt = dynmap_get(
						val *, struct lifetime *,
						block_lifetime_map(blk),
						v);

				if(lt){
					printf(" %u - %u. inter-block = %d",
							lt->start,
							lt->end,
							v->live_across_blocks);
				}else{
					printf(" no-ltime inter-block = %d",
							v->live_across_blocks);
				}
			}

			putchar('\n');
		}

		dynmap_free(vals);
	}
}
