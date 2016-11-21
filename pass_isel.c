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
		struct constraint *req_rhs)
{
	enum {
		REG_EAX = 0,
		REG_ECX = 2,
		REG_EDX = 3
	};
	/* div, shift, cmp
	 * FIXME: this is all x86(_64) specific
	 */
	switch(isn->type){
		case ISN_OP:
			switch(isn->u.op.op){
				default:
					return;
				case op_div:
				case op_mod:
				{
					/* a/b
					 * a -> %eax
					 * b -> div operand, reg or mem
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
		isn *isn, struct constraint const *req)
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
		assert(loc);

		/* TODO: assert(!regt_is_valid(req->reg[1]) && "todo"); */

		desired.where = NAME_IN_REG;
		memcpy(&desired.u.reg, &req->reg, sizeof(desired.u.reg));

		if(loc->where == NAME_NOWHERE){
			/* we can set the value directly to be what we want */
			memcpy(loc, &desired, sizeof(*loc));

		}else if(location_eq(loc, &desired)){
			/* fine */

		}else{
			/* need to go via an ABI temp */
			val *abi = val_new_abi_reg(req->reg[0], val_type(v));
			isn *copy;

			copy = isn_copy(abi, v);
			isn_insert_before(isn_to_constrain, copy);
			isn_replace_val_with_val(isn_to_constrain, v, abi, REPLACE_READS);
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
	struct constraint req_lhs = { 0 }, req_rhs = { 0 };

	populate_constraints(isn, &req_lhs, &req_rhs);

	if(req_lhs.val)
		gen_constraint_isns(isn, &req_lhs, 0);
	if(req_rhs.val)
		gen_constraint_isns(isn, &req_rhs, 0);
}

static void isel_reserve_cisc_block(block *block, void *vctx)
{
	isn *i;

	(void)vctx;

	for(i = block_first_isn(block); i; i = i->next){
		isel_reserve_cisc_isn(i);
	}
}

static void isel_reserve_cisc(block *entry, const struct target *target)
{
	blocks_traverse(entry, isel_reserve_cisc_block, NULL, NULL);
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

	isel_reserve_cisc(entry, target);
	isel_create_spills(fn, target);
}
