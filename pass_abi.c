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
#include "isn_struct.h"
#include "location.h"

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
	function *func;
	uniq_type_list *utl;
	const struct target *target;
	unsigned *uniq_index_per_func;
};

static void regpass_state_init(
		struct regpass_state *state,
		unsigned *uniq_index_per_func)
{
	memset(state, 0, sizeof(*state));
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

	assert(!type_is_void(argty));

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
		uniq_type_list *utl,
		type *argty,
		val *argval,
		const enum regoverlay_direction direction)
{
	/* structsplat will expand this if necessary */
	val *stack;
	isn *loadstore;
	int stackoff;

	state->stackoff += type_size(argty); /* FIXME: align */

	stackoff = direction == OVERLAY_FROM_REGS
			? -state->stackoff
			: /* TODO: bottomstack? */state->stackoff;

	stack = val_new_stack(stackoff, type_get_ptr(utl, argty));

	loadstore = (direction == OVERLAY_FROM_REGS ? isn_load : isn_store)(argval, stack);

	ISN_APPEND_OR_SET(state->abi_copies, loadstore);
}

static void create_arg_reg_overlay_isns(
		struct regpass_state *const state,
		const struct typeclass *const regclass,
		const struct regset *regs,
		const struct isn_insertion *insertion,
		uniq_type_list *utl,
		val *argval/*nullable*/,
		type *argty,
		const enum regoverlay_direction overlay_direction,
		const bool add_clobber)
{
	/* we either have rdx:rax (or a subset)
	 * or xmm1:xmm0 (or a subset),
	 * so we read from memory into those regs */
	/* first arg: */
	val *spilt_arg;
	val *ptrcast;
	int i;
	unsigned bytes_to_copy = type_size(argty);
	isn *current_isn = NULL;
	isn *ptrcast_isn;

	/* if it's a struct (or larger than machine word size),
	 * we need to spill it */

	if(!type_is_struct(argty)){
		/* simply assign to the argval from the abival */
		const int is_fp = !!(regclass->regs[0] & SSE);
		unsigned *const reg_index = is_fp ? &state->fp_idx : &state->int_idx;

		assert(regclass->regs[1] == NO_CLASS);

		if(argval){
			val *abiv;
			isn *copy;
			const regt regtouse = regset_nth(regs, *reg_index, is_fp);

			abiv = val_new_reg(
					regtouse,
					argty); /* argty is acceptable here */

			copy = isn_copy(
					overlay_direction == OVERLAY_FROM_REGS ? argval : abiv,
					overlay_direction == OVERLAY_FROM_REGS ? abiv : argval);

			/* this is an instruction involving an abi register/stack slot,
			 * so needs to go into abi_copies and dealt with later */
			ISN_APPEND_OR_SET(state->abi_copies, copy);

			if(add_clobber)
				isn_add_reg_clobber(insertion->at, regtouse);
		}

		++*reg_index;
		return;
	}

	if(!argval){
		/* unused, just alter the state as if we used it */
		for(i = 0; i < 2 && regclass->regs[i] != NO_CLASS; i++){
			const int is_fp = !!(regclass->regs[i] & SSE);
			unsigned *const reg_index = is_fp ? &state->fp_idx : &state->int_idx;
			++*reg_index;
		}
		return;
	}

	/* struct is already an lvalue, don't need to spill */
	spilt_arg = argval;
	ptrcast = val_new_localf(
			type_get_ptr(utl, type_get_sizet(utl)),
			false,
			"spill_cast.%d",
			(*state->uniq_index_per_func)++);
	ptrcast_isn = isn_ptrcast(spilt_arg, ptrcast);
	assert(!current_isn);
	current_isn = ptrcast_isn;


	for(i = 0; i < 2 && regclass->regs[i] != NO_CLASS; i++){
		const int is_fp = !!(regclass->regs[i] & SSE);
		unsigned *const reg_index = is_fp ? &state->fp_idx : &state->int_idx;
		type *regty;
		isn *ptradd_isn;
		val *abiv, *ptradd, *temporary, *ptrcast2;
		const regt regtouse = regset_nth(regs, *reg_index, is_fp);

		if(add_clobber)
			isn_add_reg_clobber(insertion->at, regtouse);

		regty = type_get_primitive(
				utl,
				type_primitive_less_or_equal(
					bytes_to_copy,
					is_fp));
		abiv = val_new_reg(regtouse, regty);

		temporary = val_new_localf(
				regty, false,
				"abi.%d.%d",
				i, (*state->uniq_index_per_func)++);

		if(!type_eq(regty, type_get_sizet(utl))){
			isn *load_cast;

			ptrcast2 = val_new_localf(regty, false, "recast.%d", (*state->uniq_index_per_func)++);
			load_cast = isn_ptrcast(ptrcast, ptrcast2);
			isn_insert_after(current_isn, load_cast);
			current_isn = load_cast;
		}else{
			ptrcast2 = ptrcast;
		}

		/* compute spill position for register */
		ptradd = val_new_localf(
				type_get_ptr(utl, regty),
				false,
				"ptr.%d.%d",
				i,
				(*state->uniq_index_per_func)++,
				type_to_str(regty));
		ptradd_isn = isn_ptradd(
				ptrcast2,
				val_new_i(i, regty),
				ptradd);

		isn_insert_after(current_isn, ptradd_isn);
		current_isn = ptradd_isn;

		/* copy from abiv -> local register,
		 * or vice-versa */
		if(overlay_direction == OVERLAY_FROM_REGS){
			isn *store;
			isn *copy;

			copy = isn_copy(temporary, abiv);
			ISN_APPEND_OR_SET(state->abi_copies, copy);

			/* copy from abiv -> spilt arg (inside struct) */
			store = isn_store(temporary, ptradd);
			isn_insert_after(current_isn, store);
			current_isn = store;
		}else{
			isn *load, *copy;

			/* load from inside struct / arg */
			load = isn_load(temporary, ptradd);
			isn_insert_after(current_isn, load);
			current_isn = load;

			/* move to abi reg */
			copy = isn_copy(abiv, temporary);
			ISN_APPEND_OR_SET(state->abi_copies, copy);
		}

		bytes_to_copy -= type_size(regty);
		++*reg_index;
	}

	if(insertion->before)
		isns_insert_before(insertion->at, current_isn);
	else
		isns_insert_after(insertion->at, current_isn);
}

static void classify_and_create_abi_isns_for_arg(
		const struct regset *regs,
		val *argval /*nullable*/, type *argty,
		struct regpass_state *const state,
		uniq_type_list *utl,
		isn *const insertion_point,
		const enum regoverlay_direction direction)
{
	struct typeclass cls;

	classify_type(argty, &cls);

	if(!cls.inmem){
		/* check if we've exhausted our argument registers */
		unsigned nsse = (cls.regs[0] == SSE) + (cls.regs[1] == SSE);
		unsigned nint = (cls.regs[0] == INT) + (cls.regs[1] == INT);

		if((nint && state->int_idx + nint > regset_int_count(regs))
		|| (nsse && state->fp_idx + nsse > regset_fp_count(regs)))
		{
			cls.inmem = 1;
		}
	}

	if(cls.inmem){
		if(!argval){
			/* no dependent state */
			return;
		}
		convert_incoming_arg_stack(state, utl, argty, argval, direction);

	}else{
		struct isn_insertion insertion;

		insertion.before = true;
		insertion.at = insertion_point;

		create_arg_reg_overlay_isns(
				state,
				&cls,
				regs,
				&insertion,
				utl,
				argval,
				argty,
				direction,
				false);
	}
}

static void add_state_isns_helper(val *v, isn *i, void *ctx)
{
	isn *implicituse = ctx;
	(void)i;

	if(!val_is_reg(v))
		return;

	isn_implicit_use_add(implicituse, v);
}

static void insert_state_isns(
		struct regpass_state *state,
		isn *insertion_point,
		bool insert_before,
		bool with_end)
{
	isn *iu_start, *iu_end, *i;
	isn **p_iu_end = &iu_end;

	if(!state->abi_copies)
		return;

	if(with_end){
		p_iu_end = &iu_end;
	}else{
		p_iu_end = NULL; /*once we've assigned, it's free*/
	}

	isn_implicit_use(&iu_start, p_iu_end);

	/* emit ABI copies as the first thing the function does: */
	for(i = isn_first(state->abi_copies); i; i = isn_next(i)){
		isn_on_all_vals(i, add_state_isns_helper, iu_start);
	}

	if(p_iu_end){
		isn_insert_after(isn_last(state->abi_copies), iu_end);
	}

	/* add an implicit use at the start to ensure any operations (e.g. spill)
	 * on abi regs respect the entire set of abi regs we have at this point */
	isn_insert_before(isn_first(state->abi_copies), iu_start);

	(insert_before ? isns_insert_before : isns_insert_after)(
			insertion_point, state->abi_copies);

	state->abi_copies = NULL;
}

static val *stret_ptr_stash(
		type *retty,
		block *entry,
		const struct regset *regs,
		uniq_type_list *utl,
		struct regpass_state *const state)
{
	isn *save_alloca;
	isn *save_store;
	val *alloca_val;
	val *abiv;
	type *stptr_ty = type_get_ptr(utl, retty);

	alloca_val = val_new_localf(type_get_ptr(utl, stptr_ty), true, ".stret");
	save_alloca = isn_alloca(alloca_val);

	abiv = val_new_reg(regset_nth(regs, 0, 0), stptr_ty);
	save_store = isn_store(abiv, alloca_val);

	ISN_APPEND_OR_SET(state->abi_copies, save_alloca);
	ISN_APPEND_OR_SET(state->abi_copies, save_store);

	return alloca_val;
}

static void convert_incoming_args(
		function *fn,
		val **const stret_stash_out,
		uniq_type_list *utl,
		const struct regset *regs,
		block *const entry,
		unsigned *const uniq_index_per_func)
{
	/* need to consider:
	 * - int/ptr regs
	 * - float regs
	 * - stret
	 * - struct arguments
	 */
	dynarray *args;
	type *retty = type_func_call(function_type(fn), &args, NULL);
	unsigned i;
	struct regpass_state state = { 0 };
	const bool voidret = type_is_void(retty);

	state.uniq_index_per_func = uniq_index_per_func;

	if(!voidret){
		struct typeclass retcls;
		classify_type(retty, &retcls);

		if(retcls.inmem){
			assert(type_is_struct(retty));
			/* store stret pointer for return later */
			*stret_stash_out = stret_ptr_stash(retty, entry, regs, utl, &state);
			state.int_idx++;
		}
	}

	for(i = 0; i < function_arg_count(fn); i++){
		val *argval = function_arg_val(fn, i);
		type *argty = dynarray_ent(args, i);
		/* ^ maybe null if unused - must still go through the motions for subsequent args */

		classify_and_create_abi_isns_for_arg(
				regs, argval, argty, &state, utl,
				block_first_isn(entry),
				OVERLAY_FROM_REGS);
	}

	insert_state_isns(&state, block_first_isn(entry), true, false);
}

static isn *convert_call(
		isn *inst,
		val *fnret,
		type *retty, /* may not be val_type(fnret) because of stret */
		struct regpass_state *arg_state,
		unsigned *const uniq_index_per_func,
		const struct target *target,
		uniq_type_list *utl)
{
	struct isn_insertion insertion;
	struct typeclass cls;

	insertion.before = true;
	insertion.at = inst;

	if(type_is_void(retty))
		goto out;

	classify_type(retty, &cls);

	if(cls.inmem){
		isn *alloca;
		isn *load;
		val *stret_alloca;

		stret_alloca = val_new_localf(
				type_get_ptr(utl, retty),
				true,
				"stret.%d",
				(*uniq_index_per_func)++);

		alloca = isn_alloca(stret_alloca);
		isn_insert_before(inst, alloca);

		/* pass the stret pointer in the first argument */
		create_arg_reg_overlay_isns(
				arg_state,
				&cls,
				&target->abi.arg_regs,
				&insertion,
				utl,
				stret_alloca,
				val_type(stret_alloca),
				OVERLAY_TO_REGS,
				true);

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
				&target->abi.ret_regs,
				&insertion,
				utl,
				fnret,
				retty,
				OVERLAY_FROM_REGS,
				true);

		insert_state_isns(&ret_state, inst, false, true);
		regpass_state_deinit(&ret_state);
	}

out:
	return isn_next(inst);
}

static void remove_args_from_call(isn *call)
{
	size_t i;

	assert(call->type == ISN_CALL);

	dynarray_iter(&call->u.call.args, i){
		val_release(dynarray_ent(&call->u.call.args, i));
	}
	dynarray_reset(&call->u.call.args);
}

static isn *convert_outgoing_args_and_call_isn(
		isn *inst,
		unsigned *const uniq_index_per_func,
		const struct target *target,
		uniq_type_list *utl)
{
	struct regpass_state arg_state = { 0 };
	size_t i;
	isn *const next = isn_next(inst);
	val *fnret; type *retty;
	val *fnval; type *fnty;
	dynarray *fnargs; dynarray *arg_tys;

	if(!isn_call_getfnval_ret_args(inst, &fnval, &fnret, &fnargs))
		return next;

	regpass_state_init(&arg_state, uniq_index_per_func);

	fnty = type_deref(val_type(fnval));
	retty = type_func_call(fnty, &arg_tys, /*variadic*/NULL);

	/* first, check for stret / allocate stret args, etc */
	convert_call(
			inst,
			fnret,
			retty,
			&arg_state,
			uniq_index_per_func,
			target,
			utl);

	for(i = 0; i < dynarray_count(fnargs); i++){
		val *argval = dynarray_ent(fnargs, i);

		classify_and_create_abi_isns_for_arg(
				&target->abi.arg_regs, argval, val_type(argval), &arg_state, utl,
				inst, OVERLAY_TO_REGS);
	}

	insert_state_isns(&arg_state, inst, true, true);
	regpass_state_deinit(&arg_state);

	remove_args_from_call(inst);

	return next;
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
		const struct target *target, uniq_type_list *utl, function *func)
{
	struct convert_out_ctx ctx = { 0 };

	ctx.target = target;
	ctx.utl = utl;

	function_onblocks(func, convert_outgoing_args_and_call_block, &ctx);
}

static isn *convert_return_isn(isn *inst, struct convert_ret_ctx *ctx)
{
	val *stret_stash = ctx->stret_stash;
	uniq_type_list *utl = ctx->utl;
	unsigned *const uniq_index_per_func = ctx->uniq_index_per_func;
	const struct target *target = ctx->target;
	val *retval = isn_is_ret(inst);
	type *retty;

	if(!retval)
		goto out;
	if(val_is_undef(retval))
		goto out;

	retty = type_func_call(function_type(ctx->func), NULL, NULL);
	if(type_is_struct(retty)){
		if(stret_stash){
			/* return via memory */
			val *stret_tmp = val_new_localf(val_type(retval), false, "stret_load");
			isn *load = isn_load(stret_tmp, stret_stash);
			isn *store = isn_memcpy(stret_tmp, retval);
			val *eax = val_new_reg(regset_nth(&target->abi.ret_regs, 0, 0), val_type(stret_tmp));
			/* need to reload, as we're in a different block after the memcpy is expanded */
			isn *ret_stash = isn_load(eax, stret_stash);

			isn_insert_before(inst, load);
			isn_insert_after(load, store);
			isn_insert_after(store, ret_stash);
		}else{
			struct typeclass cls;
			struct regpass_state state;
			struct isn_insertion insertion;

			classify_type(retty, &cls);
			assert(!cls.inmem && "stashed for mem-ret but not inmem");

			regpass_state_init(&state, uniq_index_per_func);

			insertion.before = true;
			insertion.at = inst;

			create_arg_reg_overlay_isns(
					&state,
					&cls,
					&target->abi.ret_regs,
					&insertion,
					utl,
					retval,
					retty,
					OVERLAY_TO_REGS,
					false);

			insert_state_isns(&state, inst, true, true);
			regpass_state_deinit(&state);
		}

	}else if(type_is_float(retty, 1)){
		assert(0 && "todo: fpret");

	}else if(type_is_void(retty)){

	}else{
		val *abiv;
		isn *movret;

		assert(type_is_int(retty) || type_deref(retty));

		abiv = val_new_reg(regset_nth(&target->abi.ret_regs, 0, 0), retty);
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
		i = convert_return_isn(i, ctx);
	}
}

static void convert_returns(
		val *stret_stash,
		uniq_type_list *utl,
		const struct target *target,
		function *func,
		unsigned *const uniq_index_per_func)
{
	struct convert_ret_ctx ctx;
	ctx.stret_stash = stret_stash;
	ctx.utl = utl;
	ctx.target = target;
	ctx.func = func;
	ctx.uniq_index_per_func = uniq_index_per_func;

	function_onblocks(func, convert_return, &ctx);
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
			&target->abi.arg_regs,
			entry,
			&uniq_index_per_func);

	convert_returns(
			stret_stash,
			unit_uniqtypes(unit),
			target,
			fn,
			&uniq_index_per_func);

	convert_outgoing_args_and_call(target, unit_uniqtypes(unit), fn);
}
