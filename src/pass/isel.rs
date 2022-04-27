use crate::unit::Unit;

pub struct Pass;

impl super::Pass for Pass {
    fn run(&mut self, _unit: &mut Unit) {
    }
}

/*
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
#include "imath.h"
#include "mem.h"
#include "builtins.h"
#include "location.h"

#define ISEL_DEBUG 1

struct isel_ctx
{
	const struct target *target;
	unit *unit;
	function *fn;
	block *block;
};

/* FIXME: this is all x86(_64) specific */
enum {
	REG_EAX = 0,
	REG_ECX = 2,
	REG_EDX = 3
};

struct constraint {
	enum location_constraint req;
	regt reg[2]; /* union if more needed */
	val *val;
	int size_req_primitive;
};

struct constrain_ctx {
	const struct target *target;
	unit *unit;
	function *fn;
};

struct func_and_utl {
	function *fn;
	uniq_type_list *utl;
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
			"reg.for.%s.%u",
			val_kind_to_str(v->kind),
			(unsigned)(long)v);

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

	if(loc && location_is_reg(loc->where))
		return;

	if(!loc || !val_can_be_assigned_reg(v)){
		val *reg = copy_val_to_reg(v, isn_to_constrain);
		loc = val_location(reg);
	}else{
		assert(loc->where == NAME_NOWHERE);
	}

	/* put it in any reg: */
	loc->where = NAME_IN_REG_ANY;
	loc->constraint = CONSTRAINT_REG;
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
	desired.constraint = CONSTRAINT_REG;
	memcpy(&desired.u.reg, &req->reg, sizeof(desired.u.reg));

	if(location_eq(loc, &desired)){
		/* fine */
		return;
	}

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

	}else{
		/* Need to go via an ABI temp.
		 * We can be sure this reg isn't in use/overlapping as isel will never
		 * generate values with a lifetime greater than the instruction for which
		 * they're used.
		 */
		val *abi;
		isn *copy;
via_temp:
		abi = val_new_reg(req->reg[0], val_type(v));

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

static void constrain_to_size(val **const out_v, isn *isn_to_constrain, int size_req_primitive, uniq_type_list *utl)
{
	val *const v = *out_v;
	type *curtype = val_type(v);
	const enum type_primitive *curprim = type_primitive(curtype);
	isn *i;
	val *out;

	assert(curprim);
	assert(size_req_primitive);

	if(*curprim == size_req_primitive)
		return;

	assert(type_primitive_size(*curprim) > type_primitive_size(size_req_primitive));

	out = val_new_localf(
			type_get_primitive(utl, size_req_primitive),
			false,
			"reg.for.size.%u",
			(unsigned)(long)v);

	i = isn_trunc(v, out);
	isn_insert_before(isn_to_constrain, i);
	isn_replace_val_with_val(isn_to_constrain, v, out, REPLACE_INPUTS);

	*out_v = out;
}

static void constrain_to_mem(val *v, isn *isn_to_constrain, bool postisn, uniq_type_list *utl, function *fn)
{
	unsigned stack_off = function_alloc_stack_space(fn, val_type(v));
	val *mem = val_new_stack(stack_off, type_get_ptr(utl, val_type(v)));
	isn *spill = isn_store(v, mem);
	struct location *loc = val_location(v);

	assert(!postisn);
	isn_insert_before(isn_to_constrain, spill);

	isn_replace_val_with_val(isn_to_constrain, v, mem, REPLACE_INPUTS | REPLACE_OUTPUTS);

	if(loc)
		loc->constraint = CONSTRAINT_MEM;
}

static void gen_constraint_isns(
		isn *isn_to_constrain,
		struct constraint const *req,
		bool postisn,
		uniq_type_list *utl,
		function *fn)
{
	/* We don't know where `req->val` will end up so we can pick any constraint,
	 * within reason. Regalloc will work around us later on. Attempt to pick
	 * constant if the value is a constant, otherwise go for reg if available.
	 */
	val *v = req->val;
	struct location *loc;

	if(req->size_req_primitive){
		constrain_to_size(&v, isn_to_constrain, req->size_req_primitive, utl);
	}

	if(v->kind == LITERAL && req->req & CONSTRAINT_CONST){
		assert(type_is_int(v->ty) || type_deref(v->ty));
		/* constraint met */
		return;
	}

	if(req->req & CONSTRAINT_MEM && val_is_mem(v))
		goto out;

	if(req->req & CONSTRAINT_REG){
		if(!regt_is_valid(req->reg[0])){
			constrain_to_reg_any(v, isn_to_constrain);

			assert(!regt_is_valid(req->reg[1]));
			goto out;
		}

		assert(!regt_is_valid(req->reg[1]) && "TODO");

		constrain_to_reg_specific(req, v, isn_to_constrain, postisn);
		goto out;
	}

	if(req->req & CONSTRAINT_MEM){
		if(!val_can_be_assigned_mem(v)){
			assert(!val_is_mem(v) && "should've been checked above");
			constrain_to_mem(v, isn_to_constrain, postisn, utl, fn);
		}
		goto out;
	}

	/* no constraint */
	assert(req->req == 0 && "couldn't constrain value");
out:
	/* stash the reg/mem constraint for later */
	loc = val_location(v);
	if(loc)
		loc->constraint = req->req;
}

static void isel_create_ptradd_isn(isn *i, type *steptype, val *rhs)
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

static void isel_create_ptrsub_isn(isn *i)
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

static bool is_valid_constraint_set(
		val *inputs[2],
		val *output,
		const struct backend_isn_constraint *constraint)
{
	struct {
		unsigned ninputs, noutputs;
	} vals, constraints = { 0 };
	unsigned i;

	vals.noutputs = !!output;
	vals.ninputs = !!inputs[0] + !!inputs[1];

	for(i = 0; i < MAX_OPERANDS && constraint->category[i]; i++){
		enum operand_category cat = constraint->category[i];

		constraints.ninputs += !!(cat & OPERAND_INPUT);
		constraints.noutputs += !!(cat & OPERAND_OUTPUT);
	}

	return vals.ninputs == constraints.ninputs
		&& vals.noutputs == constraints.noutputs;
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

		if(!is_valid_constraint_set(inputs, output, &isn->constraints[i]))
			continue;

		/* how many conversions for this constrant-set? */
		for(j = 0; j < MAX_OPERANDS && isn->constraints[i].category[j]; j++){
			bool match = true;

			if((isn->constraints[i].category[j] & OPERAND_MASK_PLAIN) == OPERAND_IMPLICIT){
				/* doesn't use any inputs or outputs explicitly */
				continue;
			}

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
		val *v,
		enum operand_category cat,
		isn *fi,
		bool postisn,
		uniq_type_list *utl,
		function *fn)
{
	struct constraint constraint = { 0 };

	if(ISEL_DEBUG){
		fprintf(stderr, "  potential conversion:\n");
		fprintf(stderr, "    %s -> %s\n", val_str(v), operand_category_to_str(cat));
	}

	assert(v);

	cat &= OPERAND_MASK_PLAIN;
	switch(cat){
		case OPERAND_REG:
			constraint.req = CONSTRAINT_REG;
			constraint.reg[0] = regt_make_invalid();
			constraint.reg[1] = regt_make_invalid();
			break;
		case OPERAND_MEM_PTR:
		case OPERAND_MEM_CONTENTS:
			constraint.req = CONSTRAINT_MEM;
			break;
		case OPERAND_INT:
			constraint.req = CONSTRAINT_CONST;
			break;
		case OPERAND_IMPLICIT:
		case OPERAND_INPUT:
		case OPERAND_OUTPUT:
		case OPERAND_ADDRESSED:
			assert(0 && "unreachable");
	}

	constraint.val = v;
	gen_constraint_isns(fi, &constraint, postisn, utl, fn);
}

static void isel_generic(
		isn *fi,
		const struct backend_isn *bi,
		uniq_type_list *utl,
		function *fn)
{
	const struct backend_isn_constraint *bestmatch;
	unsigned conversions_required;
	val *inputs[2], *output;
	unsigned i;
	unsigned input_index;

	if(ISEL_DEBUG)
		fprintf(stderr, "isel %s \"%s\"\n", isn_type_to_str(fi->type), bi->mnemonic);

	isn_vals_get(fi, inputs, &output);

	bestmatch = find_isn_bestmatch(bi, fi, inputs, output, &conversions_required);

	if(ISEL_DEBUG){
		fprintf(stderr, "  bestmatch:\n");
		if(bestmatch){
			for(i = 0; bestmatch->category[i]; i++)
				fprintf(stderr, "    categories[%d] = %s\n",
						i, operand_category_to_str(bestmatch->category[i]));
		}else{
			fprintf(stderr, "    no bestmatch!\n");
		}

		fprintf(stderr, "  for:\n");

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

	assert(bestmatch && "cannot satisfy/isel instruction");

	input_index = 0;
	for(i = 0; i < MAX_OPERANDS && bestmatch->category[i]; i++){
		if((bestmatch->category[i] & OPERAND_MASK_PLAIN) == OPERAND_IMPLICIT)
			continue;

		if(bestmatch->category[i] & OPERAND_INPUT){
			val *v;

			assert(input_index < 2);
			v = inputs[input_index++];
			assert(v && "input underflow");

			gen_constraint_isns_for_op_category(v, bestmatch->category[i], fi, false, utl, fn);
		}
		if(bestmatch->category[i] & OPERAND_OUTPUT){
			gen_constraint_isns_for_op_category(output, bestmatch->category[i], fi, true, utl, fn);
		}
	}
}

static void isel_any(
		isn *fi,
		struct isel_ctx *ctx)
{
	/* TODO */
	const struct target_arch_isn *arch_isn = &ctx->target->arch.instructions[fi->type];
	const struct backend_isn *bi;

	if(isn_is_noop(fi))
		return;
	if(fi->type == ISN_MEMCPY)
		return;

	if(arch_isn->custom_isel && arch_isn->custom_isel(fi, ctx->target))
		return;
	bi = arch_isn->backend_isn;
	if(!bi)
		return;

	isel_generic(fi, bi, unit_uniqtypes(ctx->unit), ctx->fn);
}

static void isel_op_x86_idiv_extend(isn *i)
{
	bool is_signed = false;

	switch(i->u.op.op){
		case op_sdiv:
		case op_smod:
			is_signed = true;
		case op_udiv:
		case op_umod:
		{
			type *const opty = val_type(i->u.op.lhs);
			unsigned const optysz = type_size(opty);
			const regt reg_edx = regt_make(REG_EDX, 0);

			if(optysz < 4)
				break;

			/* we'll be doing a
				* a    cltd - edx:eax
				* or a cqto - rdx:rax
				* or mov $0, %[er]dx (if unsigned)
				*/

			if(is_signed){
				isn *edx_ext;
				struct string str;

				assert((optysz == 4 || optysz == 8) && "unreachable");

				str.str = xstrdup(optysz == 4 ? "cltd" : "cqto");
				str.len = strlen(str.str);

				edx_ext = isn_asm(&str);

				isn_add_reg_clobber(edx_ext, reg_edx);
				isn_insert_before(i, edx_ext);
			}else{
				val *edx = val_new_reg(reg_edx, opty);
				isn *use_start, *use_end;
				isn *edx_set = isn_copy(edx, val_new_i(0, opty));

				isn_implicit_use(&use_start, &use_end);

				isn_implicit_use_add(use_start, edx);

				isn_insert_before(i, edx_set);
				isn_insert_before(i, use_start);
				isn_insert_after(i, use_end);
			}
			break;
		}

		default:
			break;
	}
}

static void isel_op_x86_idiv_shift_regs(isn *isn, uniq_type_list *utl, function *fn)
{
	struct constraint req_lhs = { 0 }, req_rhs = { 0 }, req_ret = { 0 };

	switch(isn->u.op.op){
		case op_udiv:
		case op_sdiv:
		case op_umod:
		case op_smod:
		{
			const int is_div = (isn->u.op.op == op_sdiv || isn->u.op.op == op_udiv);
			/* r = a/b
			 * a -> %eax
			 * b -> div operand, reg or mem
			 * r -> (op == /) ? %eax : %edx
			 */
#ifdef TODO
			struct machine_operand *idiv_operands[2];

			idiv_operands[0] = isel_mov2reg_specific(isn->u.op.lhs, REG_EAX);
			idiv_operands[1] = isel_mov2reg_or_mem(isn->u.op.rhs);

			isel_emit("idiv", countof(idiv_operands), idiv_operands);

			req_lhs.req = CONSTRAINT_REG;
			req_lhs.reg[0] = regt_make(REG_EAX, 0);
			req_lhs.reg[1] = regt_make_invalid();
			req_lhs.val = isn->u.op.lhs;

			/* %edx s/zext is handled by isel_pad_cisc_isn() */

			req_rhs.req = CONSTRAINT_REG | CONSTRAINT_MEM;
			req_rhs.reg[0] = regt_make_invalid();
			req_rhs.reg[1] = regt_make_invalid();
			req_rhs.val = isn->u.op.rhs;

			req_ret.req = CONSTRAINT_REG;
			req_ret.reg[0] = regt_make(is_div ? REG_EAX : REG_EDX, 0);
			req_ret.reg[1] = regt_make_invalid();
			req_ret.val = isn->u.op.res;
#endif
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
			req_lhs.req = CONSTRAINT_REG | CONSTRAINT_MEM;
			req_lhs.reg[0] = regt_make_invalid();
			req_lhs.reg[1] = regt_make_invalid();
			req_lhs.val = isn->u.op.lhs;

			req_rhs.req = CONSTRAINT_REG | CONSTRAINT_CONST;
			req_rhs.reg[0] = regt_make(REG_ECX, 0);
			req_rhs.reg[1] = regt_make_invalid();
			req_rhs.val = isn->u.op.rhs;
			req_rhs.size_req_primitive = i1;
			break;
		}

		default:
			break;
	}

	if(req_lhs.val)
		gen_constraint_isns(isn, &req_lhs, false, utl, fn);
	if(req_rhs.val)
		gen_constraint_isns(isn, &req_rhs, false, utl, fn);
	if(req_ret.val)
		gen_constraint_isns(isn, &req_ret, true, utl, fn);
}

static void isel_isn(isn *isn, struct isel_ctx *ctx)
{
	switch(isn->type){
		case ISN_PTRADD:
			isel_create_ptradd_isn(
					isn,
					type_deref(val_type(isn->u.ptraddsub.lhs)),
					isn->u.ptraddsub.rhs);
			break;
		case ISN_PTRSUB:
			isel_create_ptrsub_isn(isn);
			break;
		case ISN_ELEM:
		{
			type *ty = val_type(isn->u.elem.lval);

			if(type_array_element(ty)){
				isel_create_ptradd_isn(
						isn,
						type_deref(val_type(isn->u.elem.res)),
						isn->u.elem.index);
			}else{
				/* backend handles offsetting */
			}
			break;
		}

		case ISN_OP:
		{
			isel_op_x86_idiv_extend(isn);
#ifdef TODO
			isel_op_x86_idiv_shift_regs(isn, utl, fn);
#endif
			break;
		}

		default:
			break;
	}

	isel_any(isn, ctx);
}

static void isel_block(block *block, void *vctx)
{
	struct isel_ctx *ctx = vctx;
	isn *const first = block_first_isn(block);
	isn *i;

	ctx->block = block;

	isns_flag(first, true);

	for(i = block_first_isn(block); i; i = i->next)
		if(i->flag)
			isel_isn(i, ctx);
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
	struct isel_ctx ctx = { 0 };

	if(!entry){
		assert(function_is_forward_decl(fn));
		return;
	}

	ctx.target = target;
	ctx.unit = unit;
	ctx.fn = fn;
	ctx.block = NULL;

	function_onblocks(fn, isel_block, &ctx);
}
*/
