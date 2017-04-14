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
			enum { REG_ECX = 2 };
			req_lhs->req = REQ_REG | REQ_MEM;
			req_lhs->reg[0] = regt_make_invalid();
			req_lhs->reg[1] = regt_make_invalid();
			req_lhs->val = isn->u.cmp.lhs;

			req_rhs->req = REQ_CONST | REQ_REG | REQ_MEM;
			req_rhs->reg[0] = regt_make(REG_ECX, 0);
			req_rhs->reg[1] = regt_make_invalid();
			req_rhs->val = isn->u.cmp.rhs;
			break;
		}

		default:
			return;
	}
}

static void gen_constraint_isns(
		isn *isn_to_constrain, struct constraint const *req, int postisn)
{
	/* We don't know where `req->val` will end up so we can pick any constraint,
	 * within reason. Regalloc will work around us later on. Attempt to pick
	 * constant if the value is a constant, otherwise go for reg if available.
	 */
	val *v = req->val;

	if(v->kind == LITERAL && req->req & REQ_CONST){
		assert(type_is_int(v->ty));
		/* constraint met */
		return;
	}

	if(req->req & REQ_REG){
		struct location *loc = val_location(v);
		struct location desired;

		if(!loc){
			val *reg = val_new_localf(val_type(v), "reg.for.lit");
			isn *copy;

			assert(v->kind == LITERAL);
			copy = isn_copy(reg, v);
			isn_insert_before(isn_to_constrain, copy);
			isn_replace_val_with_val(isn_to_constrain, v, reg, REPLACE_READS);

			v = reg;
			loc = val_location(v);
		}

		if(!regt_is_valid(req->reg[0])){
			/* it's in a reg, done */
			assert(!regt_is_valid(req->reg[1]));
			return;
		}

		/* TODO: assert(!regt_is_valid(req->reg[1]) && "todo"); */

		desired.where = NAME_IN_REG;
		memcpy(&desired.u.reg, &req->reg, sizeof(desired.u.reg));

		if(loc->where == NAME_NOWHERE
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
				isn_replace_val_with_val(isn_to_constrain, v, abi, REPLACE_WRITES);
			}else{
				copy = isn_copy(abi, v);
				isn_insert_before(isn_to_constrain, copy);
				isn_replace_val_with_val(isn_to_constrain, v, abi, REPLACE_READS);
			}
		}
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
		gen_constraint_isns(isn, &req_lhs, 0);
	if(req_rhs.val)
		gen_constraint_isns(isn, &req_rhs, 0);
	if(req_ret.val)
		gen_constraint_isns(isn, &req_ret, 1);
}

static void isel_pad_cisc_isn(isn *i)
{
	if(i->type == ISN_OP){
		/* need a zero/sign-extension of the value into edx */
		switch(i->u.op.op){
			case op_sdiv:
			case op_smod:
			{
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

	for(i = block_first_isn(block); i; i = i->next){
		isel_reserve_cisc_isn(i);
		isel_pad_cisc_isn(i);
	}
}

static void isel_reserve_cisc(block *entry, const struct target *target)
{
	blocks_traverse(entry, isel_reserve_cisc_block, NULL, NULL);
}

static void isel_create_ptradd_isn(isn *i, unit *unit)
{
	val *lhs = i->u.ptraddsub.lhs;
	val *rhs = i->u.ptraddsub.rhs;
	const unsigned step = type_size(type_deref(val_type(lhs)));
	type *rhs_ty = val_type(rhs);
	isn *mul;
	val *tmp;

	if(step == 1)
		return;
	assert(step > 0);

	/* TODO: replace literal with literal instead of using a fresh reg */

	tmp = val_new_localf(rhs_ty, "ptradd.mul");
	mul = isn_op(op_mul, rhs, val_new_i(step, rhs_ty), tmp);
	isn_insert_before(i, mul);

	isn_replace_val_with_val(i, rhs, tmp, REPLACE_READS);
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

	tmp = val_new_localf(val_type(out), "ptrsub.div");

	isn_replace_val_with_val(i, out, tmp, REPLACE_WRITES);

	div = isn_op(op_udiv, tmp, val_new_i(step, val_type(out)), out);
	isn_insert_after(i, div);
}

static void isel_create_ptrmath_isn(isn *i, unit *unit)
{
	switch(i->type){
		default:
			return;
		case ISN_PTRADD:
			isel_create_ptradd_isn(i, unit);
			break;
		case ISN_PTRSUB:
			isel_create_ptrsub_isn(i, unit);
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
	blocks_traverse(entry, isel_create_ptrmath_blk, unit, NULL);
}

static void isel_create_spills(function *fn, const struct target *target)
{
}

void pass_isel(function *fn, struct unit *unit, const struct target *target)
{
	/*
	 * - expand struct copy isns (TODO)
	 * - reserve specific instructions to use certain registers (TODO)
	 *   e.g. x86 idiv, shift, cmp
	 * - load fp constants from memory (TODO)
	 * - check constant size - if too large, need movabs (TODO)
	 * - recognise 1(%rdi, %rax, 4) ? (TODO)
	 */
	block *const entry = function_entry_block(fn, false);

	if(!entry){
		assert(function_is_forward_decl(fn));
		return;
	}

	isel_create_ptrmath(entry, unit);
	isel_reserve_cisc(entry, target);
	isel_create_spills(fn, target);
}
