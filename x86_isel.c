#include <assert.h>

#include "target.h"
#include "isn.h"
#include "isn_struct.h"
#include "val.h"
#include "val_struct.h"
#include "backend_isn.h"

#include "x86_isel.h"

void x86_isel_lea(isn *i, const struct target *target)
{
	val *v;
	enum operand_category cat;

	assert(i->type == ISN_ELEM);

	v = i->u.elem.res;
	switch(v->kind){
		case LITERAL:
		case GLOBAL:
		case ARGUMENT:
		case ALLOCA:
			assert(0 && "invalid elem");

		case LABEL:
		case FROM_ISN:
		case BACKEND_TEMP:
		case ABI_TEMP:
			break;
	}

	cat = val_operand_category(v, false) & OPERAND_MASK_PLAIN;
	switch(cat){
		case OPERAND_REG:
			return;

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
