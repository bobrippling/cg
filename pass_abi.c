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
	isn *abi_copies; /* list of isn*, %abi_... = <reg %d> */
	int stackoff;
	unsigned int_idx;
	unsigned fp_idx;
	unsigned *uniq_index_per_func;
};

struct isn_insertion
{
	isn *at;
	bool before;
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

struct convert_ret_ctx
{
	val *stret_stash;
	uniq_type_list *utl;
	const struct target *target;
};

static void regpass_state_init(
		struct regpass_state *state,
		unsigned *uniq_index_per_func)
{
	state->abi_copies = NULL;
	state->uniq_index_per_func = uniq_index_per_func;
}

static void regpass_state_deinit(struct regpass_state *state)
{
	state->abi_copies = NULL;
}

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

	fprintf(stderr, "FIXME: stack argument alignment\n");
	state->stackoff += type_size(argty);
	stack = val_new_abi_stack(state->stackoff, argty);

	store = isn_store(argval, stack);

	ISN_APPEND_OR_SET(state->abi_copies, store);
}

static void create_arg_reg_overlay_isns(
		struct regpass_state *const state,
		const struct typeclass *const regclass,
		const struct target *target,
		const struct isn_insertion *insertion,
		uniq_type_list *utl,
		val *argval,
		type *argty,
		const enum regoverlay_direction overlay_direction)
{
	/* we either have rdx:rax (or a subset)
	 * or xmm1:xmm0 (or a subset),
	 * so we read from memory into those regs */
	/* first arg: */
	isn *alloca = NULL;
	val *spilt_arg;
	int i;
	unsigned bytes_to_copy = type_size(argty);
	isn *current_isn = NULL;

	/* if it's a struct (or larger than machine word size),
	 * we need to spill it */

	if(!type_is_struct(argty)){
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
				argty); /* argty is acceptable here */

		copy = isn_copy(
				overlay_direction == OVERLAY_FROM_REGS ? argval : abiv,
				overlay_direction == OVERLAY_FROM_REGS ? abiv : argval);

		/* this is an instruction involving an abi register/stack slot,
		 * so needs to go into abi_copies and dealt with later */
		ISN_APPEND_OR_SET(state->abi_copies, copy);

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

		assert(!current_isn);
		current_isn = initial_spill;
	}

	if(overlay_direction == OVERLAY_FROM_REGS){
		/* need a local storage space for the (struct) argument */
		alloca = isn_alloca(spilt_arg);

		if(current_isn)
			isn_insert_after(current_isn, alloca);

		current_isn = alloca;
	}

	assert(current_isn);

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
				regty);

		abi_copy = val_new_localf(
				regty, "abi.%d.%d",
				i, (*state->uniq_index_per_func)++);

		/* compute spill position / elem for register */
		elemp = val_new_localf(
				type_get_ptr(utl, regty),
				"spill.%d.%d",
				i,
				(*state->uniq_index_per_func)++);
		elem_isn = isn_elem(
				spilt_arg,
				val_new_i(i, type_get_primitive(utl, i4)),
				elemp);

		isn_insert_after(current_isn, elem_isn);
		current_isn = elem_isn;

		/* copy from abiv -> local register,
		 * or vice-versa */
		if(overlay_direction == OVERLAY_FROM_REGS){
			isn *store;
			isn *copy = isn_copy(abi_copy, abiv);
			ISN_APPEND_OR_SET(state->abi_copies, copy);

			/* copy from abiv -> spilt arg (inside struct) */
			store = isn_store(abi_copy, elemp);
			assert(alloca);
			isn_insert_after(current_isn, store);
			current_isn = store;
		}else{
			isn *load, *copy;

			assert(!alloca);
			/* load from inside struct / arg */
			load = isn_load(abi_copy, elemp);
			isn_insert_after(current_isn, load);
			current_isn = load;

			/* move to abi reg */
			copy = isn_copy(abiv, abi_copy);
			ISN_APPEND_OR_SET(state->abi_copies, copy);
		}

		bytes_to_copy -= type_size(regty);
		++*reg_index;
	}

	if(overlay_direction == OVERLAY_FROM_REGS){
		/* insert the final assignment - the constructed struct */
		isn *final_load = isn_load(argval, spilt_arg);

		isn_insert_after(current_isn, final_load);
	}

	if(insertion->before)
		isns_insert_before(insertion->at, current_isn);
	else
		isns_insert_after(insertion->at, current_isn);
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
		struct isn_insertion insertion;

		insertion.before = true;
		insertion.at = insertion_point;

		create_arg_reg_overlay_isns(
				state,
				&cls,
				target,
				&insertion,
				utl,
				argval,
				argty,
				direction);
	}
}

static void add_state_isns_helper(val *v, isn *i, void *ctx)
{
	isn *implicituse = ctx;
	(void)i;

	if(!val_is_abi(v))
		return;

	isn_implicit_use_add(implicituse, v);
}

static void insert_state_isns(
		struct regpass_state *state,
		isn *insertion_point,
		bool insert_before)
{
	isn *implicituse, *i;

	if(!state->abi_copies)
		return;

	implicituse = isn_implicit_use();

	/* emit ABI copies as the first thing the function does: */
	for(i = isn_first(state->abi_copies); i; i = isn_next(i)){
		isn_on_all_vals(i, add_state_isns_helper, implicituse);
	}

	/* repurpose abi_copies to emit an implicit use of all abi values,
	 * so they're preserved up until the final point */
	isn_insert_after(isn_last(state->abi_copies), implicituse);

	(insert_before ? isns_insert_before : isns_insert_after)(
			insertion_point, state->abi_copies);

	state->abi_copies = NULL;
}

static val *stret_ptr_stash(
		type *retty,
		block *entry,
		const struct target *target,
		uniq_type_list *utl)
{
	isn *save_alloca;
	isn *save_store;
	val *alloca_val;
	val *abiv;
	type *stptr_ty = type_get_ptr(utl, retty);

	alloca_val = val_new_localf(type_get_ptr(utl, stptr_ty), ".stret");
	save_alloca = isn_alloca(alloca_val);

	assert(target->abi.arg_regs_cnt_int > 0);
	abiv = val_new_abi_reg(target->abi.arg_regs_int[0], stptr_ty);
	save_store = isn_store(abiv, alloca_val);

	isn_insert_before(block_first_isn(entry), save_alloca);
	isn_insert_after(save_alloca, save_store);

	return alloca_val;
}

static void convert_incoming_args(
		function *fn,
		val **const stret_stash_out,
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
	type *retty = type_func_call(function_type(fn), NULL, NULL);
	unsigned i;
	struct regpass_state state = { 0 };
	unsigned uniq_index_per_func = 0;
	struct typeclass retcls;

	state.uniq_index_per_func = &uniq_index_per_func;

	classify_type(retty, &retcls);
	if(retcls.inmem){
		assert(type_is_struct(retty));
		/* store stret pointer for return later */
		*stret_stash_out = stret_ptr_stash(retty, entry, target, utl);
		state.int_idx++;
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

	insert_state_isns(&state, block_first_isn(entry), true);
}

static isn *convert_call(
		isn *inst,
		val *fnret,
		struct regpass_state *arg_state,
		unsigned *const uniq_index_per_func,
		const struct target *target,
		uniq_type_list *utl)
{
	struct isn_insertion insertion;
	struct typeclass cls;

	insertion.before = true;
	insertion.at = inst;

	classify_type(val_type(fnret), &cls);

	if(cls.inmem){
		isn *alloca;
		isn *load;
		val *stret_alloca;

		stret_alloca = val_new_localf(
				type_get_ptr(utl, val_type(fnret)),
				"stret.%d",
				(*uniq_index_per_func)++);

		alloca = isn_alloca(stret_alloca);
		isn_insert_before(inst, alloca);

		/* pass the stret pointer in the first argument */
		create_arg_reg_overlay_isns(
				arg_state,
				&cls,
				target,
				&insertion,
				utl,
				stret_alloca,
				val_type(stret_alloca),
				OVERLAY_TO_REGS);

		/* afterwards we load from stret into retval */
		load = isn_load(fnret, stret_alloca);
		isn_insert_after(inst, load);

	}else{
		struct regpass_state ret_state = { 0 };

		regpass_state_init(&ret_state, uniq_index_per_func);

		insertion.before = false;

		/* copy return value(s) into inst->u.call.into_or_null */
		create_arg_reg_overlay_isns(
				&ret_state,
				&cls,
				target,
				&insertion,
				utl,
				fnret,
				val_type(fnret),
				OVERLAY_FROM_REGS);

		insert_state_isns(&ret_state, inst, false);
		regpass_state_deinit(&ret_state);
	}


	return isn_next(inst);
}

static isn *convert_outgoing_args_and_call_isn(
		isn *inst,
		unsigned *const uniq_index_per_func,
		const struct target *target,
		uniq_type_list *utl)
{
	struct regpass_state arg_state = { 0 };
	size_t i;
	val *fnret; type *retty;
	val *fnval; type *fnty;
	dynarray *fnargs; dynarray *arg_tys;

	if(!isn_call_getfnval_ret_args(inst, &fnval, &fnret, &fnargs))
		return isn_next(inst);

	regpass_state_init(&arg_state, uniq_index_per_func);

	fnty = type_deref(val_type(fnval));
	retty = type_func_call(fnty, &arg_tys, /*variadic*/NULL);

	/* first, check for stret / allocate stret args, etc */
	convert_call(inst, fnret, &arg_state, uniq_index_per_func, target, utl);

	for(i = 0; i < dynarray_count(arg_tys); i++){
		val *argval = dynarray_ent(fnargs, i);

		classify_and_create_abi_isns_for_arg(
				target, argval, &arg_state, utl,
				inst, OVERLAY_TO_REGS);
	}

	insert_state_isns(&arg_state, inst, true);
	regpass_state_deinit(&arg_state);

	return isn_next(inst);
}

static void convert_outgoing_args_and_call_block(block *blk, void *const vctx)
{
	struct convert_out_ctx *const ctx = vctx;
	isn *i = block_first_isn(blk);

	while(i){
		i = convert_outgoing_args_and_call_isn(i, &ctx->uniq_index_per_func, ctx->target, ctx->utl);
	}
}

static void convert_outgoing_args_and_call(
		const struct target *target, uniq_type_list *utl, block *const entry)
{
	struct convert_out_ctx ctx = { 0 };

	ctx.target = target;
	ctx.utl = utl;

	blocks_traverse(entry, convert_outgoing_args_and_call_block, &ctx, NULL);
}

static isn *convert_return_isn(
		isn *inst,
		val *stret_stash,
		uniq_type_list *utl,
		const struct target *target)
{
	val *retval = isn_is_ret(inst);
	type *retty;

	if(!retval)
		goto out;

	retty = val_type(retval);
	if(type_is_struct(retty)){
		if(stret_stash){
			/* return via memory */
			isn *load, *store;
			val *loadtmp = val_new_localf(
					type_get_ptr(utl, retty),
					"stret.val");

			load = isn_load(loadtmp, stret_stash);
			store = isn_store(retval, loadtmp);

			isn_insert_before(inst, load);
			isn_insert_after(load, store);
		}else{
			assert(0 && "todo: stret via regs");
		}

	}else if(type_is_float(retty, 1)){
		assert(0 && "todo: fpret");

	}else{
		val *abiv;
		isn *movret;

		assert(target->abi.ret_regs_cnt > 0);
		assert(type_is_int(retty));

		abiv = val_new_abi_reg(target->abi.ret_regs_int[0], retty);
		movret = isn_copy(abiv, retval);

		isn_insert_before(inst, movret);
	}

out:
	return isn_next(inst);
}

static void convert_return(block *blk, void *const vctx)
{
	struct convert_ret_ctx *ctx = vctx;
	isn *i = block_first_isn(blk);

	while(i){
		i = convert_return_isn(
				i,
				ctx->stret_stash,
				ctx->utl,
				ctx->target);
	}
}

static void convert_returns(
		val *stret_stash,
		uniq_type_list *utl,
		const struct target *target,
		block *entry)
{
	struct convert_ret_ctx ctx;
	ctx.stret_stash = stret_stash;
	ctx.utl = utl;
	ctx.target = target;

	blocks_traverse(entry, convert_return, &ctx, NULL);
}

void pass_abi(function *fn, unit *unit, const struct target *target)
{
	/* convert incoming and outgoing arguments
	 * into registers/stack entries */
	block *const entry = function_entry_block(fn, false);
	val *stret_stash = NULL;
	unsigned uniq_index_per_func = 0;

	if(!entry){
		assert(function_is_forward_decl(fn));
		return;
	}

	convert_incoming_args(
			fn,
			&stret_stash,
			unit_uniqtypes(unit),
			target,
			entry);

	convert_returns(
			stret_stash,
			unit_uniqtypes(unit),
			target,
			entry);

	convert_outgoing_args_and_call(target, unit_uniqtypes(unit), entry);
}
