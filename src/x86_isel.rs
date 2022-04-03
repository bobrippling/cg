#include <assert.h>

#include "target.h"
#include "isn.h"
#include "isn_struct.h"
#include "val.h"
#include "val_struct.h"
#include "backend_isn.h"
#include "type.h"

#include "x86_isel.h"

static void check_lea_result(val *v_result)
{
	enum operand_category cat;

	switch(v_result->kind){
		case UNDEF:
		case LITERAL:
		case GLOBAL:
		case ALLOCA:
		case LABEL:
			assert(0 && "invalid elem");

		case LOCAL:
			break;
	}

	cat = val_operand_category(v_result, false) & OPERAND_MASK_PLAIN;
	switch(cat){
		case OPERAND_REG:
			break;

		case OPERAND_INT:
		case OPERAND_MEM_CONTENTS:
			assert(0 && "can't lea to int/mem-contents");

		case OPERAND_MEM_PTR:
			assert(0 && "need to convert mem_ptr to reg");

		case OPERAND_IMPLICIT:
			assert(0 && "bad instruction chosen");

		case OPERAND_INPUT:
		case OPERAND_OUTPUT:
		case OPERAND_ADDRESSED:
			assert(0 && "unreachable");
	}
}

bool x86_isel_lea(isn *i, const struct target *target)
{
	struct location *loc = val_location(i->u.elem.res);

	assert(i->type == ISN_ELEM);

	check_lea_result(i->u.elem.res);

	assert(loc->where == NAME_NOWHERE || loc->where == NAME_IN_REG || loc->where == NAME_IN_REG_ANY);
	assert(loc->constraint == CONSTRAINT_NONE);
	loc->where = NAME_IN_REG_ANY;
	loc->constraint = CONSTRAINT_REG;

	/* if it's a struct, we've done everything we need to
	 * arrays may be more flexible, and we can apply isel based on the backend_isn */
	return type_is_struct(type_deref(val_type(i->u.elem.lval)));
}

bool x86_isel_op(isn *i, const struct target *target)
{
	assert(i->type == ISN_OP);

	switch(i->u.op.op){
		case op_sdiv:
		case op_smod:
		case op_udiv:
		case op_umod:
			return true; /* isel done already */
		default:
			return false;
	}
}
