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
static const char *const regclass_strs[] = {
	"NO_CLASS",
	"INT",
	"SSE"
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

static void convert_incoming_arg_regs(
		struct regpass_state *const state,
		const struct typeclass *const regclass,
		const struct target *target,
		block *const entry,
		uniq_type_list *utl,
		const unsigned argindex,
		val *argval,
		type *argty)
{
	/* we either have rdx:rax (or a subset)
	 * or xmm1:xmm0 (or a subset),
	 * so we read from memory into those regs */
	/* first arg: */
	isn *alloca, *load;
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
				argty);

		copy = isn_new(ISN_COPY);
		copy->u.copy.from = val_retain(abiv);
		copy->u.copy.to = val_retain(argval);

		dynarray_add(&state->abi_copies, copy);

		++*reg_index;
		return;
	}

	spilt_arg = val_new_localf(
			type_get_ptr(utl, argty),
			"spil.%d",
			argindex);

	/* insert the final assignment first, so we get it last */
	load = isn_new(ISN_LOAD);
	load->u.load.to = val_retain(argval);
	load->u.load.lval = val_retain(spilt_arg);
	block_insert_isn(entry, load);

	/* for each argument, we copy register,
	 * then we store it to the local struct later
	 * which helps keep regalloc from clobbering anything */
	for(i = 0; i < 2 && regclass->regs[i] != NO_CLASS; i++){
		const int is_fp = !!(regclass->regs[i] & SSE);
		const unsigned *arg_reg_array = (
				is_fp
				? target->abi.arg_regs_fp
				: target->abi.arg_regs_int);
		unsigned *const reg_index = is_fp ? &state->fp_idx : &state->int_idx;
		type *regty;
		isn *tmpisn;
		val *abiv, *elemp, *abi_copy;

		regty = type_get_primitive(
				utl,
				type_primitive_less_or_equal(
					bytes_to_copy,
					is_fp));

		abiv = val_new_abi_reg(
				arg_reg_array[*reg_index],
				argty);

		abi_copy = val_new_localf(argty, "abi.%d.%d", argindex, i);

		elemp = val_new_localf(
				type_get_ptr(utl, argty),
				"spil.%d.%d",
				argindex, i);

		/* copy from abiv -> local register */
		tmpisn = isn_new(ISN_COPY);
		tmpisn->u.copy.from = val_retain(abiv);
		tmpisn->u.copy.to = val_retain(abi_copy);
		dynarray_add(&state->abi_copies, tmpisn);

		/* the next two are in reverse as we're using block_insert_isn() */
		/* copy from abiv -> spilt arg */
		tmpisn = isn_new(ISN_STORE);
		tmpisn->u.store.from = val_retain(abi_copy);
		tmpisn->u.store.lval = val_retain(elemp);
		block_insert_isn(entry, tmpisn);

		/* compute spill for register */
		tmpisn = isn_new(ISN_ELEM);
		tmpisn->u.elem.lval = val_retain(spilt_arg);
		tmpisn->u.elem.index = val_retain(val_new_i(i, type_get_primitive(utl, i4)));
		tmpisn->u.elem.res = val_retain(elemp);
		block_insert_isn(entry, tmpisn);

		bytes_to_copy -= type_size(regty);
		++*reg_index;
	}

	/* insert the alloca last, so we get it first */
	alloca = isn_new(ISN_ALLOCA);
	alloca->u.alloca.out = val_retain(spilt_arg);
	block_insert_isn(entry, alloca);
}

static void convert_incoming_arg(
		const struct target *target,
		val *argval,
		const unsigned argindex,
		struct regpass_state *const state,
		uniq_type_list *utl,
		function *fn,
		block *const entry)
{
	const char *const argname = dynarray_ent(function_arg_names(fn), argindex);
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

	if(SHOW_CLASSIFICATION){
		printf("classified \"%s\":\n", argname);

		if(cls.inmem){
			printf("\tinmem\n");
		}else{
			int i;
			for(i = 0; i < 2; i++)
				printf("\tclass[%d] = %s\n",
						i,
						regclass_strs[cls.regs[i]]);
		}
	}

	if(cls.inmem){
		convert_incoming_arg_stack(state, argty, argval);

	}else{
		convert_incoming_arg_regs(
				state,
				&cls,
				target,
				entry,
				utl,
				argindex,
				argval,
				argty);
	}
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

	dynarray_init(&state.abi_copies);

	if(type_is_struct(retty)){
		/* need to remember this and generate the memcpy for return later */
		/* TODO */
	}

	for(i = 0; i < function_arg_count(fn); i++){
		val *argval = function_arg_val(fn, i);

		if(!argval)
			continue;

		convert_incoming_arg(target, argval, i, &state, utl, fn, entry);
	}

	/* emit ABI copies as the first thing the function does: */
	dynarray_iter(&state.abi_copies, i){
		isn *isn = dynarray_ent(&state.abi_copies, i);

		block_insert_isn(entry, isn);
	}

	dynarray_reset(&state.abi_copies);
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
}
