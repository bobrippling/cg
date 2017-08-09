#include <stddef.h>

#include "backend_isn.h"

const char *operand_category_to_str(enum operand_category cat)
{
	switch(cat){
		case OPERAND_REG: return "OPERAND_REG";
		case OPERAND_MEM_PTR: return "OPERAND_MEM_PTR";
		case OPERAND_MEM_CONTENTS: return "OPERAND_MEM_CONTENTS";
		case OPERAND_INT: return "OPERAND_INT";
	}
	return NULL;
}
