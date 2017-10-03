#include <stdio.h>
#include <assert.h>

#include "mem.h"

#include "backend_isn.h"

const char *operand_category_to_str(enum operand_category cat)
{
	static char buf[1 + sizeof "OPERAND_INPUT | OPERAND_OUTPUT | OPERAND_MEM_CONTENTS"];
	enum operand_category plain = cat & OPERAND_MASK_PLAIN;
	unsigned ioa = cat & ~OPERAND_MASK_PLAIN;
	char *p = buf;
#define remaining (sizeof(buf) - (p - buf))

	*p = '\0';
	if(ioa & OPERAND_INPUT)
		p += xsnprintf(p, remaining, "OPERAND_INPUT | ");
	if(ioa & OPERAND_OUTPUT)
		p += xsnprintf(p, remaining, "OPERAND_OUTPUT | ");
	if(ioa & OPERAND_ADDRESSED)
		p += xsnprintf(p, remaining, "OPERAND_ADDRESSED | ");

	switch(plain){
		case OPERAND_REG:
			xsnprintf(p, remaining, "OPERAND_REG");
			break;
		case OPERAND_MEM_PTR:
			xsnprintf(p, remaining,  "OPERAND_MEM_PTR");
			break;
		case OPERAND_MEM_CONTENTS:
			xsnprintf(p, remaining,  "OPERAND_MEM_CONTENTS");
			break;
		case OPERAND_INT:
			xsnprintf(p, remaining,  "OPERAND_INT");
			break;
		case OPERAND_IMPLICIT:
			xsnprintf(p, remaining,  "OPERAND_IMPLICIT");
			break;
		case OPERAND_INPUT:
		case OPERAND_OUTPUT:
		case OPERAND_ADDRESSED:
			assert(0 && "unreachable");
	}

	return buf;
}
