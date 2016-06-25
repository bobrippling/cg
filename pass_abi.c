#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "function.h"
#include "val.h"
#include "val_internal.h"
#include "target.h"
#include "isn.h"
#include "block_internal.h"
#include "type.h"
#include "type_iter.h"
#include "mem.h"

#include "pass_abi.h"

#define SHOW_CLASSIFICATION 0

enum regclass {
	NO_CLASS,
	INT,
	SSE
};

struct typeclass
{
	int inmem;
	enum regclass regs[2];
};

struct regpass_state
{
	dynarray abi_copies; /* array of isn*, %abi_... = <reg %d> */
	int stackoff;
	unsigned int_idx;
	unsigned fp_idx;
	unsigned *uniq_index_per_func;
};

enum regoverlay_direction
{
	OVERLAY_TO_REGS,
	OVERLAY_FROM_REGS
};

struct convert_out_ctx
{
	const struct target *target;
	uniq_type_list *utl;
	unsigned uniq_index_per_func;
};

static enum regclass regclass_merge(enum regclass a, enum regclass b)
{
	if(a == NO_CLASS)
		return b;
	if(b == NO_CLASS)
		return a;
	if(a == INT || b == INT)
		return INT;
	assert(a == SSE && b == SSE);
	return SSE;
}

static void classify_type(type *argty, struct typeclass *const argclass)
{
	struct type_iter *iter;
	type *ity;
	unsigned nbytes = 0;
	unsigned const tysz = type_size(argty);
	int class_index = 0;

	memset(argclass, 0, sizeof(*argclass));

	if(tysz == 0 || tysz > 16){
		argclass->inmem = 1;
		return;
	}

	iter = type_iter_new(argty);

	/* walk through all the types, until we reach 8 bytes.
	 * at that stage we decide how to pass the first 8 bytes,
	 * then we do the second 8 bytes.
	 */
	while((ity = type_iter_next(iter))){
		enum regclass curclass = NO_CLASS;

		if(type_is_float(ity, 0)){
			curclass = SSE;
		}else if(type_is_int(ity) || type_deref(ity)){
			curclass = INT;
		}else{
			assert(0 && "unhandled type");
		}

		nbytes += type_size(ity);
		if(nbytes > 8){
			class_index = 1;
			assert(nbytes <= 16);
		}else{
			assert(class_index == 0);
		}

		argclass->regs[class_index]
			= regclass_merge(argclass->regs[class_index], curclass);
	}

	assert(!argclass->inmem);

	type_iter_free(iter);
}

static void convert_incoming_arg_stack(
		struct regpass_state *const state,
		type *argty,
		val *argval)
{
	/* structsplat will expand this if necessary */
	val *stack;
	isn *store;

	/* FIXME: alignment */
	state->stackoff += type_size(argty);
	stack = val_new_abi_stack(state->stackoff, argty);

	store = isn_store(argval, stack);

	dynarray_add(&state->abi_copies, store);
}

static void create_arg_reg_overlay_isns(
		struct regpass_state *const state,
		const struct typeclass *const regclass,
		const struct target *target,
		isn *const insertion_point, /* insert before here */
		uniq_type_list *utl,
		val *argval,
		type *argty,
		const enum regoverlay_direction overlay_direction)
{
	/* we either have rdx:rax (or a subset)
	 * or xmm1:xmm0 (or a subset),
	 * so we read from memory into those regs */
	/* first arg: */
	isn *alloca;
	val *spilt_arg;
	int i;
	unsigned bytes_to_copy = type_size(argty);

	/* if it's a struct (or larger than machine word size),
	 * we need to spill it */

	if(bytes_to_copy <= 8){
		/* simply assign to the argval from the abival */
		const int is_fp = !!(regclass->regs[0] & SSE);
		const unsigned *arg_reg_array = (
				is_fp
				? target->abi.arg_regs_fp
				: target->abi.arg_regs_int);
		unsigned *const reg_index = is_fp ? &state->fp_idx : &state->int_idx;
		val *abiv;
		isn *copy;

		assert(regclass->regs[1] == NO_CLASS);

		abiv = val_new_abi_reg(
				arg_reg_array[*reg_index],
				argty); /* FIXME: argty */

		copy = isn_copy(
				overlay_direction == OVERLAY_FROM_REGS ? argval : abiv,
				overlay_direction == OVERLAY_FROM_REGS ? abiv : argval);

		/* this is an instruction involving an abi register/stack slot,
		 * so needs to go into abi_copies and dealt with later */
		dynarray_add(&state->abi_copies, copy);

		++*reg_index;
		return;
	}

	spilt_arg = val_new_localf(
			type_get_ptr(utl, argty),
			"spill.%d",
			(*state->uniq_index_per_func)++);

	if(overlay_direction == OVERLAY_TO_REGS){
		/* spill the struct to somewhere we can elem it from */
		isn *initial_spill = isn_store(argval, spilt_arg);
		isn_insert_before(insertion_point, initial_spill);
	}

	if(overlay_direction == OVERLAY_FROM_REGS){
		/* need a local storage space for the (struct) argument */
		alloca = isn_alloca(spilt_arg);
		isn_insert_before(insertion_point, alloca);
	}else{
		alloca = NULL;
	}

	for(i = 0; i < 2 && regclass->regs[i] != NO_CLASS; i++){
		const int is_fp = !!(regclass->regs[i] & SSE);
		const unsigned *arg_reg_array = (
				is_fp
				? target->abi.arg_regs_fp
				: target->abi.arg_regs_int);
		unsigned *const reg_index = is_fp ? &state->fp_idx : &state->int_idx;
		type *regty;
		isn *elem_isn;
		val *abiv, *elemp, *abi_copy;

		regty = type_get_primitive(
				utl,
				type_primitive_less_or_equal(
					bytes_to_copy,
					is_fp));

		abiv = val_new_abi_reg(
				arg_reg_array[*reg_index],
				argty); /* FIXME: argty */

		abi_copy = val_new_localf(
				argty, "abi.%d.%d",
				i, (*state->uniq_index_per_func)++);

		/* compute spill position / elem for register */
		elemp = val_new_localf(
				type_get_ptr(utl, argty),
				"spill.%d.%d",
				i,
				(*state->uniq_index_per_func)++);
		elem_isn = isn_elem(
				spilt_arg,
				val_new_i(i, type_get_primitive(utl, i4)),
				elemp);
		isn_insert_before(insertion_point, elem_isn);


		/* copy from abiv -> local register,
		 * or vice-versa */
		if(overlay_direction == OVERLAY_FROM_REGS){
			isn *store;
			isn *copy = isn_copy(abi_copy, abiv);
			dynarray_add(&state->abi_copies, copy);

			/* copy from abiv -> spilt arg (inside struct) */
			store = isn_store(abi_copy, elemp);
			assert(alloca);
			isn_insert_after(elem_isn, store);
		}else{
			isn *load, *copy;

			assert(!alloca);
			/* load from inside struct / arg */
			load = isn_load(abi_copy, elemp);
			isn_insert_after(elem_isn, load);

			/* move to abi reg */
			copy = isn_copy(abiv, abi_copy);
			dynarray_add(&state->abi_copies, copy);
		}

		bytes_to_copy -= type_size(regty);
		++*reg_index;
	}

	if(overlay_direction == OVERLAY_FROM_REGS){
		/* insert the final assignment - the constructed struct */
		isn *final_load = isn_load(argval, spilt_arg);
		isn_insert_before(insertion_point, final_load);
	}
}

static void classify_and_create_abi_isns_for_arg(
		const struct target *target,
		val *argval,
		struct regpass_state *const state,
		uniq_type_list *utl,
		isn *const insertion_point,
		const enum regoverlay_direction direction)
{
	type *argty = val_type(argval);
	struct typeclass cls;

	classify_type(argty, &cls);

	if(!cls.inmem){
		/* check if we've exhausted our argument registers */
		unsigned nsse = (cls.regs[0] == SSE) + (cls.regs[1] == SSE);
		unsigned nint = (cls.regs[0] == INT) + (cls.regs[1] == INT);

		if((nint && state->int_idx + nint > target->abi.arg_regs_cnt_int)
		|| (nsse && state->fp_idx + nsse > target->abi.arg_regs_cnt_fp))
		{
			cls.inmem = 1;
		}
	}

	if(cls.inmem){
		convert_incoming_arg_stack(state, argty, argval);

	}else{
		create_arg_reg_overlay_isns(
				state,
				&cls,
				target,
				insertion_point,
				utl,
				argval,
				argty,
				direction);
	}
}

static void add_state_isns(
		struct regpass_state *state, isn *insertion_point)
{
	unsigned i;

	/* emit ABI copies as the first thing the function does: */
	dynarray_iter(&state->abi_copies, i){
		isn *isn = dynarray_ent(&state->abi_copies, i);

		isn_insert_before(insertion_point, isn);
	}

	dynarray_reset(&state->abi_copies);
}

static void convert_incoming_args(
		function *fn,
		uniq_type_list *utl,
		const struct target *target,
		block *const entry)
{
	/* need to consider:
	 * - int/ptr regs
	 * - float regs
	 * - stret
	 * - struct arguments
	 */
	type *retty = type_func_call(type_deref(function_type(fn)), NULL, NULL);
	unsigned i;
	struct regpass_state state = { 0 };
	unsigned uniq_index_per_func = 0;
	state.uniq_index_per_func = &uniq_index_per_func;

	dynarray_init(&state.abi_copies);

	if(type_is_struct(retty)){
		/* need to remember this and generate the memcpy for return later */
		/* TODO */
	}

	for(i = 0; i < function_arg_count(fn); i++){
		val *argval = function_arg_val(fn, i);

		if(!argval)
			continue;

		classify_and_create_abi_isns_for_arg(
				target, argval, &state, utl,
				block_first_isn(entry),
				OVERLAY_FROM_REGS);
	}

	add_state_isns(&state, block_first_isn(entry));
}

static isn *convert_outgoing_args_isn(
		isn *inst,
		unsigned *const uniq_index_per_func,
		const struct target *target,
		uniq_type_list *utl)
{
	struct regpass_state state = { 0 };
	size_t i;
	type *retty;
	val *fnval; type *fnty;
	dynarray *fnargs; dynarray *arg_tys;

	if(!isn_call_getfnval_args(inst, &fnval, &fnargs))
		return isn_next(inst);

	dynarray_init(&state.abi_copies);

	state.uniq_index_per_func = uniq_index_per_func;

	fnty = type_deref(val_type(fnval));
	retty = type_func_call(fnty, &arg_tys, /*variadic*/NULL);

	/* first, check for stret */
	if(type_is_struct(retty)){
		/* TODO */
	}

	for(i = 0; i < dynarray_count(arg_tys); i++){
		val *argval = dynarray_ent(fnargs, i);

		classify_and_create_abi_isns_for_arg(
				target, argval, &state, utl,
				inst, OVERLAY_TO_REGS);
	}

	add_state_isns(&state, inst);

	return isn_next(inst);
}

static void convert_outgoing_args_block(block *blk, void *const vctx)
{
	struct convert_out_ctx *const ctx = vctx;
	isn *i = block_first_isn(blk);

	while(i){
		i = convert_outgoing_args_isn(i, &ctx->uniq_index_per_func, ctx->target, ctx->utl);
	}
}

static void convert_outgoing_args(
		const struct target *target, uniq_type_list *utl, block *const entry)
{
	struct convert_out_ctx ctx = { 0 };

	ctx.target = target;
	ctx.utl = utl;

	blocks_traverse(entry, convert_outgoing_args_block, &ctx, NULL);
}

void pass_abi(function *fn, unit *unit, const struct target *target)
{
	/* convert incoming and outgoing arguments
	 * into registers/stack entries */
	block *const entry = function_entry_block(fn, false);

	if(!entry){
		assert(function_is_forward_decl(fn));
		return;
	}

	convert_incoming_args(fn, unit_uniqtypes(unit), target, entry);
	convert_outgoing_args(target, unit_uniqtypes(unit), entry);
}
