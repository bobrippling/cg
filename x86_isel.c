#include <assert.h>

#include "target.h"
#include "isn.h"
#include "isn_struct.h"
#include "val.h"
#include "val_struct.h"
#include "backend_isn.h"

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
	assert(i->type == ISN_ELEM);

	check_lea_result(i->u.elem.res);

	return true;
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
