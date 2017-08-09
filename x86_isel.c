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

	assert(i->type == ISN_ELEM);

	v = i->u.elem.res;
	switch(v->kind){
		case LITERAL:
		case GLOBAL:
		case ARGUMENT:
		case ALLOCA:
			assert(0 && "invalid elem");

		case FROM_ISN:
		case BACKEND_TEMP:
		case ABI_TEMP:
			break;
	}

	switch(val_operand_category(v, false)){
		case OPERAND_REG:
			return;

		case OPERAND_INT:
		case OPERAND_MEM_CONTENTS:
			assert(0 && "can't lea to int/mem-contents");

		case OPERAND_MEM_PTR:
			assert(0 && "need to convert mem_ptr to reg");
	}
}
