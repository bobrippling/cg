#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
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
#include "val_struct.h"
#include "string.h"

isn *isn_new(enum isn_type t)
{
	isn *isn = xcalloc(1, sizeof *isn);
	isn->type = t;
	isn->regusemarks = regset_marks_new();
	dynarray_init(&isn->clobbers);
	return isn;
}

static bool isn_in_list(isn *list, isn *candidate)
{
	for(list = isn_first(list); list; list = isn_next(list))
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
	isn *i;

	assert(!head->prev);
	assert(!tail->next);
	for(i = isn_first(list); i; i = isn_next(i))
		assert(!isn_in_list(target, i));

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
	isn *i;

	assert(!head->prev);
	assert(!tail->next);
	for(i = isn_first(list); i; i = isn_next(i))
		assert(!isn_in_list(target, i));

	tail->next = target->next;
	if(target->next)
		target->next->prev = tail;

	head->prev = target;
	target->next = head;
}

void isns_detach(isn *first_new)
{
	if(first_new->prev){
		assert(first_new->prev->next == first_new);
		first_new->prev->next = NULL;
	}

	first_new->prev = NULL;
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

size_t isns_count(isn *i)
{
	size_t n = 0;
	for(; i; n++, i = isn_next(i));
	return n;
}

void isn_free_1(isn *isn)
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
		case ISN_MEMCPY:
			val_release(isn->u.memcpy.from);
			val_release(isn->u.memcpy.to);
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
		case ISN_JMP_COMPUTED:
			val_release(isn->u.jmpcomp.target);
			break;
		case ISN_LABEL:
			val_release(isn->u.lbl.val);
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
		case ISN_ASM:
			free(isn->u.as.str);
			break;
		case ISN_IMPLICIT_USE_START:
		{
			size_t i;

			dynarray_iter(&isn->u.implicit_use_start.vals, i)
				val_release(dynarray_ent(&isn->u.implicit_use_start.vals, i));
			dynarray_reset(&isn->u.implicit_use_start.vals);
			break;
		}
		case ISN_IMPLICIT_USE_END:
			break;
	}

	regset_marks_free(isn->regusemarks);
	dynarray_reset(&isn->clobbers);
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
		case ISN_MEMCPY:return "memcpy";
		case ISN_EXT_TRUNC: return "zext/sext/trunc";
		case ISN_RET:    return "ret";
		case ISN_BR:     return "br";
		case ISN_JMP:    return "jmp";
		case ISN_JMP_COMPUTED: return "jmp*";
		case ISN_LABEL:  return "label";
		case ISN_CALL:   return "call";
		case ISN_PTR2INT:return "ptr2int";
		case ISN_INT2PTR:return "int2ptr";
		case ISN_PTRCAST:return "ptrcast";
		case ISN_ASM: return "asm";
		case ISN_IMPLICIT_USE_START: return "implicit_use_start";
		case ISN_IMPLICIT_USE_END: return "implicit_use_end";
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

	assert(type_size(val_type(lhs)) == type_size(val_type(rhs)) || !op_operands_must_match(op));
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

	assert(type_size(val_type(lval)) == type_size(val_type(index)));

	isn = isn_new(ISN_ELEM);
	isn->u.elem.lval = lval;
	isn->u.elem.index = index;
	isn->u.elem.res = res;
	return isn;
}

isn *isn_ptradd(val *lhs, val *rhs, val *out)
{
	isn *isn;

	assert(type_eq(val_type(lhs), val_type(out)));
	assert(type_is_int(val_type(rhs)));

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

	assert(type_eq(val_type(lhs), val_type(rhs)));
	assert(type_is_int(val_type(out)));

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

isn *isn_memcpy(val *to, val *from)
{
	isn *isn;

	val_retain(to);
	val_retain(from);

	assert(val_type(to) == val_type(from));

	isn = isn_new(ISN_MEMCPY);
	isn->u.memcpy.from = from;
	isn->u.memcpy.to = to;
	return isn;
}

void isn_implicit_use(isn **const start, isn **const end)
{
	*start = isn_new(ISN_IMPLICIT_USE_START);
	if(end){
		*end = isn_new(ISN_IMPLICIT_USE_END);
		(*end)->u.implicit_use_end.link = *start;
	}

	dynarray_init(&(*start)->u.implicit_use_start.vals);
}

void isn_implicit_use_add(isn *i, val *v)
{
	assert(i->type == ISN_IMPLICIT_USE_START);
	dynarray_add(&i->u.implicit_use_start.vals, val_retain(v));
}

void isn_add_reg_clobber(isn *i, regt reg)
{
	dynarray_add(&i->clobbers, (void *)(uintptr_t)reg);
}

dynarray *isn_clobbers(isn *i)
{
	return &i->clobbers;
}

isn *isn_alloca(val *v)
{
	isn *isn;

	assert(type_deref(val_type(v))
			&& "pointer expected for alloca next type");

	val_retain(v);
	assert(v->kind == ALLOCA);

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
	type *fn_ret, *into_type;
	dynarray *argtys;
	bool variadic;

	if(into)
		val_retain(into);
	val_retain(fn);

	dynarray_iter(args, i){
		val_retain(dynarray_ent(args, i));
	}

	fn_ret = type_func_call(type_deref(val_type(fn)), &argtys, &variadic);

	into_type = val_type(into);
	if(type_is_struct(fn_ret)){
		into_type = type_deref(into_type);
	}
	assert(!into || type_eq(into_type, fn_ret));

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

isn *isn_asm(struct string *str)
{
	isn *isn = isn_new(ISN_ASM);

	memcpy(&isn->u.as, str, sizeof(*str));
	memset(str, 0, sizeof(*str));

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

isn *isn_jmp_computed(val *v)
{
	isn *isn;

	isn = isn_new(ISN_JMP_COMPUTED);
	isn->u.jmpcomp.target = val_retain(v);

	return isn;
}

isn *isn_label(val *lblval)
{
	isn *isn = isn_new(ISN_LABEL);
	isn->u.lbl.val = val_retain(lblval);
	return isn;
}

bool isn_is_noop(isn *isn)
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
		case ISN_JMP_COMPUTED:
		case ISN_BR:
		case ISN_CALL:
		case ISN_ASM:
		case ISN_MEMCPY:
			break;

		case ISN_LABEL:
		case ISN_IMPLICIT_USE_START:
		case ISN_IMPLICIT_USE_END:
		case ISN_PTRCAST: /* assumes all pointers are the same size and representation */
			return true;

		case ISN_PTR2INT:
		case ISN_INT2PTR:
			return val_size(isn->u.ptr2int.from) == val_size(isn->u.ptr2int.to);
	}

	return false;
}

bool isn_defines_val(isn *isn, struct val *v)
{
	switch(isn->type){
		case ISN_STORE:
		case ISN_RET:
		case ISN_JMP:
		case ISN_JMP_COMPUTED:
		case ISN_LABEL:
		case ISN_BR:
		case ISN_IMPLICIT_USE_START:
		case ISN_IMPLICIT_USE_END:
		case ISN_ASM:
		case ISN_MEMCPY:
			return false;

		case ISN_LOAD:
			return v == isn->u.load.to;

		case ISN_ALLOCA:
			return v == isn->u.alloca.out;

		case ISN_ELEM:
			return v == isn->u.elem.res;

		case ISN_PTRADD:
		case ISN_PTRSUB:
			return v == isn->u.ptraddsub.out;

		case ISN_OP:
			return v == isn->u.op.res;

		case ISN_CMP:
			return v == isn->u.cmp.res;

		case ISN_COPY:
			return v == isn->u.copy.to;

		case ISN_EXT_TRUNC:
			return v == isn->u.ext.to;

		case ISN_PTR2INT:
		case ISN_INT2PTR:
			return v == isn->u.ptr2int.to;

		case ISN_PTRCAST:
			return v == isn->u.ptrcast.to;

		case ISN_CALL:
			return v == isn->u.call.into;
	}
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

		case ISN_MEMCPY:
			fn(current->u.memcpy.to, current, ctx);
			fn(current->u.memcpy.from, current, ctx);
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

		case ISN_JMP_COMPUTED:
			fn(current->u.jmpcomp.target, current, ctx);
			break;

		case ISN_LABEL:
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

		case ISN_ASM:
			break;

		case ISN_IMPLICIT_USE_START:
		case ISN_IMPLICIT_USE_END:
		{
			dynarray *vals = isn_implicit_use_vals(current);
			size_t i;

			dynarray_iter(vals, i)
				fn(dynarray_ent(vals, i), current, ctx);
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


static void isn_dump1(isn *i, FILE *f)
{
	switch(i->type){
		case ISN_STORE:
		{
			fprintf(f, "\tstore %s, %s\n",
					val_str_rn(0, i->u.store.lval),
					val_str_rn(1, i->u.store.from));
			break;
		}

		case ISN_LOAD:
		{
			fprintf(f, "\t%s = load %s\n",
					val_str_rn(0, i->u.load.to),
					val_str_rn(1, i->u.load.lval));

			break;
		}

		case ISN_ALLOCA:
		{
			fprintf(f, "\t%s = alloca %s\n",
					val_str(i->u.alloca.out),
					type_to_str(type_deref(val_type(i->u.alloca.out))));
			break;
		}

		case ISN_ELEM:
		{
			fprintf(f, "\t%s = elem %s, %s\n",
					val_str_rn(0, i->u.elem.res),
					val_str_rn(1, i->u.elem.lval),
					val_str_rn(2, i->u.elem.index));
			break;
		}

		case ISN_PTRADD:
		case ISN_PTRSUB:
		{
			fprintf(f, "\t%s = ptr%s %s, %s\n",
					val_str_rn(0, i->u.ptraddsub.out),
					i->type == ISN_PTRADD ? "add" : "sub",
					val_str_rn(1, i->u.ptraddsub.lhs),
					val_str_rn(2, i->u.ptraddsub.rhs));
			break;
		}

		case ISN_CMP:
		{
			fprintf(f, "\t%s = %s %s, %s\n",
					val_str_rn(0, i->u.cmp.res),
					op_cmp_to_str(i->u.cmp.cmp),
					val_str_rn(1, i->u.cmp.lhs),
					val_str_rn(2, i->u.cmp.rhs));
			break;
		}

		case ISN_OP:
		{
			fprintf(f, "\t%s = %s %s, %s\n",
					val_str_rn(0, i->u.op.res),
					op_to_str(i->u.op.op),
					val_str_rn(1, i->u.op.lhs),
					val_str_rn(2, i->u.op.rhs));
			break;
		}

		case ISN_COPY:
		{
			fprintf(f, "\t%s = %s\n",
					val_str_rn(0, i->u.copy.to),
					val_str_rn(1, i->u.copy.from));
			break;
		}

		case ISN_MEMCPY:
		{
			fprintf(f, "\tmemcpy %s, %s\n",
					val_str_rn(0, i->u.memcpy.to),
					val_str_rn(1, i->u.memcpy.from));
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

			fprintf(f, "\t%s = %s %s, %s\n",
					val_str_rn(0, i->u.ext.to),
					ext_kind,
					type_to_str(val_type(i->u.ext.to)),
					val_str_rn(1, i->u.ext.from));
			break;
		}

		case ISN_INT2PTR:
		case ISN_PTR2INT:
		{
			fprintf(f, "\t%s = %s %s, %s\n",
					val_str_rn(0, i->u.ptr2int.to),
					isn_type_to_str(i->type),
					type_to_str(val_type(i->u.ptr2int.to)),
					val_str_rn(1, i->u.ptr2int.from));
			break;
		}

		case ISN_PTRCAST:
		{
			fprintf(f, "\t%s = ptrcast %s, %s\n",
					val_str_rn(0, i->u.ptrcast.to),
					type_to_str(val_type(i->u.ptrcast.to)),
					val_str_rn(1, i->u.ptrcast.from));
			break;
		}

		case ISN_RET:
		{
			fprintf(f, "\tret %s\n", val_str(i->u.ret));
			break;
		}

		case ISN_JMP:
		{
			fprintf(f, "\tjmp $%s\n", i->u.jmp.target->lbl);
			break;
		}

		case ISN_JMP_COMPUTED:
		{
			fprintf(f, "\tjmp *%s\n", val_str(i->u.jmpcomp.target));
			break;
		}

		case ISN_LABEL:
		{
			fprintf(f, "\tlabel %s\n", val_str(i->u.lbl.val));
			break;
		}

		case ISN_CALL:
		{
			size_t argi;
			const char *sep = "";

			fprintf(f, "\t");

			fprintf(f, "%s = ", val_str(i->u.call.into));

			fprintf(f, "call %s(", val_str(i->u.call.fn));

			dynarray_iter(&i->u.call.args, argi){
				val *arg = dynarray_ent(&i->u.call.args, argi);

				fprintf(f, "%s%s", sep, val_str(arg));

				sep = ", ";
			}

			fprintf(f, ")\n");
			break;
		}

		case ISN_BR:
		{
			fprintf(f, "\tbr %s, $%s, $%s\n",
					val_str(i->u.branch.cond),
					i->u.branch.t->lbl,
					i->u.branch.f->lbl);
			break;
		}

		case ISN_ASM:
		{
			fprintf(f, "\tasm \"");
			dump_escaped_string(&i->u.as, f);
			fprintf(f, "\"\n");
			break;
		}

		case ISN_IMPLICIT_USE_START:
		case ISN_IMPLICIT_USE_END:
		{
			const bool is_start = i->type == ISN_IMPLICIT_USE_START;
			size_t j;
			const char *sep = "";
			dynarray *vals = isn_implicit_use_vals(i);

			fprintf(f, "\timplicit_use_%s ", is_start ? "start" : "end");

			dynarray_iter(vals, j){
				fprintf(f, "%s%s", sep, val_str(dynarray_ent(vals, j)));
				sep = ", ";
			}
			fprintf(f, "\n");
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

void isns_flag(isn *start, bool f)
{
	for(; start; start = isn_next(start))
		start->flag = f;
}

#include "dynmap.h"
#include "val_struct.h"
static void get_named_val(val *v, isn *isn, void *ctx)
{
	(void)isn;

	switch(v->kind){
		case LITERAL:
		case UNDEF:
		case GLOBAL:
		case LABEL:
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
void isn_dump(isn *const head, block *blk, FILE *f)
{
	size_t idx = 0;
	isn *i;

	for(i = head; i; i = i->next){
		if(i->skip)
			continue;

		if(SHOW_LIFE)
			fprintf(f, "[%zu] ", idx++);
		if(SHOW_LIFE)
			fprintf(f, "[%p] ", i);

		isn_dump1(i, f);
	}

	if(SHOW_LIFE || SHOW_TYPE){
		dynmap *vals = dynmap_new(val *, 0, val_hash);

		for(i = head; i; i = i->next)
			isn_on_live_vals(i, get_named_val, vals);

		val *v;
		for(idx = 0; (v = dynmap_key(val *, vals, idx)); idx++){
			fprintf(f, "# %s:", val_str(v));

			if(SHOW_TYPE)
				fprintf(f, " %s", type_to_str(val_type(v)));

			if(SHOW_LIFE){
				struct lifetime *lt = dynmap_get(
						val *, struct lifetime *,
						block_lifetime_map(blk),
						v);

				if(lt){
					fprintf(f, " %p - %p. inter-block = %d",
							lt->start,
							lt->end,
							v->live_across_blocks);
				}else{
					fprintf(f, " no-ltime inter-block = %d",
							v->live_across_blocks);
				}
			}

			fputc('\n', f);
		}

		dynmap_free(vals);
	}
}
