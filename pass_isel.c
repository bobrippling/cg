#include <stdio.h>
#include <assert.h>

#include "function.h"

#include "pass_isel.h"

#include "isn_struct.h"
#include "reg.h"
#include "val.h"
#include "val_struct.h"
#include "isn.h"
#include "isn_replace.h"
#include "type.h"
#include "unit.h"
#include "target.h"
#include "backend_isn.h"

#define ISEL_DEBUG 0

/* FIXME: this is all x86(_64) specific */
enum {
	REG_EAX = 0,
	REG_ECX = 2,
	REG_EDX = 3
};

struct constraint {
	enum {
		REQ_REG   = 1 << 0,
		REQ_CONST = 1 << 1,
		REQ_MEM   = 1 << 2
	} req;
	regt reg[2]; /* union if more needed */
	val *val;
};

static void populate_constraints(
		isn *isn,
		struct constraint *req_lhs,
		struct constraint *req_rhs,
		struct constraint *req_ret)
{
	/* div, shift, cmp
	 */
	switch(isn->type){
		case ISN_OP:
			switch(isn->u.op.op){
				default:
					return;
				case op_sdiv:
				case op_smod:
				case op_udiv:
				case op_umod:
				{
					const int is_div = (isn->u.op.op == op_sdiv || isn->u.op.op == op_udiv);
					/* r = a/b
					 * a -> %eax
					 * b -> div operand, reg or mem
					 * r -> (op == /) ? %eax : %edx
					 */
					req_lhs->req = REQ_REG;
					req_lhs->reg[0] = regt_make(REG_EAX, 0);
					req_lhs->reg[1] = regt_make_invalid();
					req_lhs->val = isn->u.op.lhs;

					if(type_size(req_lhs->val->ty) >= 4){
						/* we'll be doing a
						 * a    cltd - edx:eax
						 * or a cqto - rdx:rax
						 * or mov $0, %[er]dx (if unsigned)
						 */
						req_lhs->reg[1] = regt_make(REG_EDX, 0);
					}

					req_rhs->req = REQ_REG | REQ_MEM;
					req_rhs->reg[0] = regt_make_invalid();
					req_rhs->reg[1] = regt_make_invalid();
					req_rhs->val = isn->u.op.rhs;

					req_ret->req = REQ_REG;
					req_ret->reg[0] = regt_make(is_div ? REG_EAX : REG_EDX, 0);
					req_ret->reg[1] = regt_make_invalid();
					req_ret->val = isn->u.op.res;
					break;
				}

				case op_shiftl:
				case op_shiftr_logic:
				case op_shiftr_arith:
				{
					/* a << b
					 * a: reg/mem
					 * b: %cl or const
					 */
					req_lhs->req = REQ_REG | REQ_MEM;
					req_lhs->reg[0] = regt_make_invalid();
					req_lhs->reg[1] = regt_make_invalid();
					req_lhs->val = isn->u.op.lhs;

					req_rhs->req = REQ_REG | REQ_CONST;
					req_rhs->reg[0] = regt_make(REG_ECX, 0);
					req_rhs->reg[1] = regt_make_invalid();
					req_rhs->val = isn->u.op.rhs;
					break;
				}
			}
			break;

		case ISN_CMP:
		{
			/* a <cmp> b
			 *  ->   b     a
			 * cmp const, r/m
			 * cmp r,     r/m
			 * cmp r/m,   r
			 * cmp r/m,   r
			 * cmp r/m,   r
			 */
			req_lhs->req = REQ_REG | REQ_MEM;
			req_lhs->reg[0] = regt_make_invalid();
			req_lhs->reg[1] = regt_make_invalid();
			req_lhs->val = isn->u.cmp.lhs;

			req_rhs->req = REQ_CONST | REQ_REG | REQ_MEM;
			req_rhs->reg[0] = regt_make_invalid();
			req_rhs->reg[1] = regt_make_invalid();
			req_rhs->val = isn->u.cmp.rhs;
			break;
		}

		default:
			return;
	}
}

static val *copy_val_to_reg(val *v, isn *isn_to_constrain)
{
	val *reg;
	isn *copy;

	reg = val_new_localf(
			val_type(v),
			false,
			"reg.for.%s.%d",
			val_kind_to_str(v->kind),
			(int)v);

	copy = isn_copy(reg, v);
	isn_insert_before(isn_to_constrain, copy);
	isn_replace_val_with_val(isn_to_constrain, v, reg, REPLACE_INPUTS);

	return reg;
}

static void constrain_to_reg_any(
		val *v,
		isn *isn_to_constrain)
{
	struct location *loc = val_location(v);

	if(!loc){
		val *reg = copy_val_to_reg(v, isn_to_constrain);
		loc = val_location(reg);
	}else{
		assert(loc->where == NAME_NOWHERE);
	}

	/* put it in any reg: */
	loc->where = NAME_IN_REG_ANY;
}

static void constrain_to_reg_specific(
		const struct constraint *const req,
		val *v,
		isn *isn_to_constrain,
		bool postisn)
{
	struct location *loc = val_location(v);
	struct location desired;

	if(!loc){
		val *reg = copy_val_to_reg(v, isn_to_constrain);
		loc = val_location(reg);
	}

	desired.where = NAME_IN_REG;
	memcpy(&desired.u.reg, &req->reg, sizeof(desired.u.reg));

	if(loc->where == NAME_NOWHERE
	|| loc->where == NAME_IN_REG_ANY
	|| (loc->where == NAME_IN_REG && !regt_is_valid(loc->u.reg)))
	{
		/* We can set the value directly to be what we want.
		 * ... in the optimal case, but generally we need to be more indirect.
		 * See isel.md
		 */
		/*memcpy(loc, &desired, sizeof(*loc)); - optimal */
		goto via_temp; /* - general */

	}else if(location_eq(loc, &desired)){
		/* fine */

	}else{
		/* Need to go via an ABI temp.
		 * We can be sure this reg isn't in use/overlapping as isel will never
		 * generate values with a lifetime greater than the instruction for which
		 * they're used.
		 */
		val *abi;
		isn *copy;
via_temp:
		abi = val_new_abi_reg(req->reg[0], val_type(v));

		assert(regt_is_valid(req->reg[0]));

		if(postisn){
			copy = isn_copy(v, abi);
			isn_insert_after(isn_to_constrain, copy);
			isn_replace_val_with_val(isn_to_constrain, v, abi, REPLACE_OUTPUTS);
		}else{
			copy = isn_copy(abi, v);
			isn_insert_before(isn_to_constrain, copy);
			isn_replace_val_with_val(isn_to_constrain, v, abi, REPLACE_INPUTS);
		}
	}
}

static void gen_constraint_isns(
		isn *isn_to_constrain, struct constraint const *req, bool postisn)
{
	/* We don't know where `req->val` will end up so we can pick any constraint,
	 * within reason. Regalloc will work around us later on. Attempt to pick
	 * constant if the value is a constant, otherwise go for reg if available.
	 */
	val *v = req->val;

	if(v->kind == LITERAL && req->req & REQ_CONST){
		assert(type_is_int(v->ty) || type_deref(v->ty));
		/* constraint met */
		return;
	}

	if(req->req & REQ_REG){
		if(!regt_is_valid(req->reg[0])){
			constrain_to_reg_any(v, isn_to_constrain);

			assert(!regt_is_valid(req->reg[1]));
			return;
		}

		/* TODO: assert(!regt_is_valid(req->reg[1]) && "todo"); */

		constrain_to_reg_specific(req, v, isn_to_constrain, postisn);
		return;
	}

	if(req->req & REQ_MEM){
		struct location *loc = val_location(v);
		assert(loc);

		loc->where = NAME_SPILT;

		assert(0 && "TODO");
		return;
	}

	/* no constraint */
}

static void isel_reserve_cisc_isn(isn *isn)
{
	struct constraint req_lhs = { 0 }, req_rhs = { 0 }, req_ret = { 0 };

	populate_constraints(isn, &req_lhs, &req_rhs, &req_ret);

	if(req_lhs.val)
		gen_constraint_isns(isn, &req_lhs, false);
	if(req_rhs.val)
		gen_constraint_isns(isn, &req_rhs, false);
	if(req_ret.val)
		gen_constraint_isns(isn, &req_ret, true);
}

static void isel_pad_cisc_isn(isn *i)
{
	if(i->type == ISN_OP){
		switch(i->u.op.op){
			case op_sdiv:
			case op_smod:
			{
				/* FIXME: need a zero/sign-extension of the value into edx */
				break;
			}
			case op_udiv:
			case op_umod:
			{
				type *opty = val_type(i->u.op.lhs);
				val *edx = val_new_abi_reg(regt_make(REG_EDX, 0), opty);
				val *zero = val_new_i(0, opty);
				isn *copy = isn_copy(edx, zero);
				isn *use = isn_implicit_use();
				isn_implicit_use_add(use, edx);

				isn_insert_before(i, copy);
				isn_insert_after(i, use);
				break;
			}
			default:
				break;
		}
	}
}

static void isel_reserve_cisc_block(block *block, void *vctx)
{
	isn *i;

	(void)vctx;

	isns_flag(block_first_isn(block), true);

	for(i = block_first_isn(block); i; i = i->next){
		if(i->flag){
			isel_reserve_cisc_isn(i);
			isel_pad_cisc_isn(i);
		}
	}
}

static void isel_reserve_cisc(block *entry)
{
	blocks_traverse(entry, isel_reserve_cisc_block, NULL);
}

static void isel_create_ptradd_isn(isn *i, unit *unit, type *steptype, val *rhs)
{
	const unsigned step = type_size(steptype);
	type *rhs_ty = val_type(rhs);
	isn *mul;
	val *tmp;

	if(step == 1)
		return;
	assert(step > 0);

	/* TODO: replace literal with literal instead of using a fresh reg */

	tmp = val_new_localf(rhs_ty, false, "ptradd.mul");
	mul = isn_op(op_mul, rhs, val_new_i(step, rhs_ty), tmp);
	isn_insert_before(i, mul);

	isn_replace_val_with_val(i, rhs, tmp, REPLACE_INPUTS);
}

static void isel_create_ptrsub_isn(isn *i, unit *unit)
{
	val *lhs = i->u.ptraddsub.lhs;
	val *out = i->u.ptraddsub.out;
	const unsigned step = type_size(type_deref(val_type(lhs)));
	isn *div;
	val *tmp;

	if(step == 1)
		return;
	assert(step > 0);

	/* TODO: replace literal with literal instead of using a fresh reg */

	tmp = val_new_localf(val_type(out), false, "ptrsub.div");

	isn_replace_val_with_val(i, out, tmp, REPLACE_OUTPUTS);

	div = isn_op(op_udiv, tmp, val_new_i(step, val_type(out)), out);
	isn_insert_after(i, div);
}

static void isel_create_ptrmath_isn(isn *i, unit *unit)
{
	switch(i->type){
		default:
			return;
		case ISN_PTRADD:
			isel_create_ptradd_isn(
					i,
					unit,
					type_deref(val_type(i->u.ptraddsub.lhs)),
					i->u.ptraddsub.rhs);
			break;
		case ISN_PTRSUB:
			isel_create_ptrsub_isn(i, unit);
			break;
		case ISN_ELEM:
			isel_create_ptradd_isn(
					i,
					unit,
					type_deref(val_type(i->u.elem.res)),
					i->u.elem.index);
			break;
	}
}

static void isel_create_ptrmath_blk(block *block, void *vctx)
{
	isn *i;
	unit *unit = vctx;

	(void)vctx;

	for(i = block_first_isn(block); i; i = i->next){
		isel_create_ptrmath_isn(i, unit);
	}
}

static void isel_create_ptrmath(block *const entry, unit *unit)
{
	blocks_traverse(entry, isel_create_ptrmath_blk, unit);
}

static bool operand_type_convertible(
		enum operand_category from, enum operand_category to)
{
	from &= OPERAND_MASK_PLAIN;
	to &= OPERAND_MASK_PLAIN;

	if(to == OPERAND_INT)
		return from == OPERAND_INT;

	return true;
}

static bool should_deref(isn *isn, val *val)
{
	bool dereference = false;

	/* explicit / forced dereference */
	switch(isn->type){
		case ISN_STORE:
			dereference |= val == isn->u.store.lval;
			break;
		case ISN_LOAD:
			dereference |= val == isn->u.load.lval;
			break;
		default:
			break;
	}

	return dereference;
}

static const struct backend_isn_constraint *find_isn_bestmatch(
		const struct backend_isn *isn,
		struct isn *fi,
		val *inputs[2],
		val *output,
		unsigned *const out_conversions_required)
{
	const int max = countof(isn->constraints);
	int i, bestmatch_i = -1;
	unsigned bestmatch_conversions = ~0u;

	for(i = 0; i < max && isn->constraints[i].category[0]; i++){
		unsigned nmatches = 0;
		unsigned conversions_required = 0;
		unsigned j, nargs;
		unsigned input_index = 0;
		bool abort_this = false;

		/* how many conversions for this constrant-set? */
		for(j = 0; j < MAX_OPERANDS && isn->constraints[i].category[j]; j++){
			bool match = true;

			if(isn->constraints[i].category[j] & OPERAND_INPUT){
				val *v;
				enum operand_category cat;

				assert(input_index < 2);
				v = inputs[input_index];
				input_index++;
				assert(v && "input underflow");
				cat = val_operand_category(v, should_deref(fi, v));

				if(!val_operand_category_matches(cat, isn->constraints[i].category[j])){
					match = false;

					if(operand_type_convertible(cat, isn->constraints[i].category[j])){
						conversions_required++;
					}else{
						abort_this = true;
					}
				}
			}
			if(isn->constraints[i].category[j] & OPERAND_OUTPUT){
				enum operand_category cat = val_operand_category(output, should_deref(fi, output));

				if(!val_operand_category_matches(cat, isn->constraints[i].category[j])){
					match = false;

					if(operand_type_convertible(cat, isn->constraints[i].category[j])){
						conversions_required++;
					}else{
						abort_this = true;
					}
				}
			}

			if(match)
				nmatches++;
		}
		nargs = j;

		if(nmatches == nargs){
			assert(conversions_required == 0);
			bestmatch_conversions = conversions_required;
			bestmatch_i = i;
			break;
		}

		if(abort_this)
			continue;

		/* we can convert all operands, we may have a new best match */
		if(conversions_required < bestmatch_conversions /* best */){
			bestmatch_conversions = conversions_required;
			bestmatch_i = i;
		}
	}

	*out_conversions_required = bestmatch_conversions;

	if(bestmatch_i != -1)
		return &isn->constraints[bestmatch_i];

	return NULL;
}

static void gen_constraint_isns_for_op_category(
		val *v, enum operand_category cat, isn *fi, bool postisn)
{
	struct constraint constraint;

	assert(v);

	cat &= OPERAND_MASK_PLAIN;
	switch(cat){
		case OPERAND_REG:
			constraint.req = REQ_REG;
			constraint.reg[0] = regt_make_invalid();
			constraint.reg[1] = regt_make_invalid();
			break;
		case OPERAND_MEM_PTR:
		case OPERAND_MEM_CONTENTS:
			constraint.req = REQ_MEM;
			break;
		case OPERAND_INT:
			constraint.req = REQ_CONST;
			break;
		default:
			/* no constraint */
			return;
	}

	constraint.val = v;
	gen_constraint_isns(fi, &constraint, postisn);
}

static void isel_generic(isn *fi, const struct target *target, const struct backend_isn *bi)
{
	const struct backend_isn_constraint *bestmatch;
	unsigned conversions_required;
	val *inputs[2], *output;
	unsigned i;
	unsigned input_index;

	if(ISEL_DEBUG)
		fprintf(stderr, "isel \"%s\"\n", bi->mnemonic);

	isn_vals_get(fi, inputs, &output);

	bestmatch = find_isn_bestmatch(bi, fi, inputs, output, &conversions_required);

	if(bestmatch && ISEL_DEBUG){
		fprintf(stderr, "  bestmatch:\n");
		for(i = 0; bestmatch->category[i]; i++)
			fprintf(stderr, "    categories[%d] = %s\n",
					i, operand_category_to_str(bestmatch->category[i]));

		for(i = 0; i < 2 && inputs[i]; i++){
			enum operand_category cat = val_operand_category(
					inputs[i],
					should_deref(fi, inputs[i]));

			fprintf(stderr, "    inputs[%d] = %s\n",
					i, operand_category_to_str(cat));
		}

		if(output){
			fprintf(stderr, "    output = %s\n",
					operand_category_to_str(
						val_operand_category(output, should_deref(fi, output))));
		}
	}

	if(ISEL_DEBUG)
		fprintf(stderr, "  %d conversions required\n", conversions_required);

	if(conversions_required == 0)
		return;
	assert(bestmatch && "cannot satisfy/isel instruction");

	input_index = 0;
	for(i = 0; i < MAX_OPERANDS && bestmatch->category[i]; i++){
		if(bestmatch->category[i] & OPERAND_INPUT){
			val *v;

			assert(input_index < 2);
			v = inputs[input_index++];
			assert(v && "input underflow");

			gen_constraint_isns_for_op_category(v, bestmatch->category[i], fi, false);
		}
		if(bestmatch->category[i] & OPERAND_OUTPUT){
			gen_constraint_isns_for_op_category(output, bestmatch->category[i], fi, true);
		}
	}
}

static void isel_constrain_isn(isn *fi, const struct target *target)
{
	const struct target_arch_isn *arch_isn = &target->arch.instructions[fi->type];
	const struct backend_isn *bi;

	if(arch_isn->custom_isel){
		arch_isn->custom_isel(fi, target);
		return;
	}
	bi = arch_isn->backend_isn;
	if(!bi)
		return;

	isel_generic(fi, target, bi);
}

static void isel_constrain_isns_block(block *block, void *vctx)
{
	const struct target *target = vctx;
	isn *i;

	isn *const first = block_first_isn(block);

	isns_flag(first, true);

	for(i = block_first_isn(block); i; i = i->next){
		if(i->flag)
			isel_constrain_isn(i, target);
	}
}

static void isel_constrain_isns(block *entry, const struct target *target)
{
	blocks_traverse(entry, isel_constrain_isns_block, (void *)target);
}

void pass_isel(function *fn, struct unit *unit, const struct target *target)
{
	/*
	 * # isel
	 * - reserve specific instructions to use certain registers
	 *   e.g. x86 idiv, shift, cmp
	 * - constrain all isns, e.g. imul
	 *
	 * # housekeeping after other passes
	 * - expand struct copy isns (TODO)
	 * - load fp constants from memory (TODO)
	 * - check constant size - if too large, need movabs (TODO)
	 *
	 * # optimisation
	 * - recognise 1(%rdi, %rax, 4) ? (TODO)
	 */
	block *const entry = function_entry_block(fn, false);

	if(!entry){
		assert(function_is_forward_decl(fn));
		return;
	}

	isel_create_ptrmath(entry, unit);
	isel_reserve_cisc(entry);
	isel_constrain_isns(entry, target);
}
