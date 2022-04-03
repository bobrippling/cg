#ifndef X86_ISNS_H
#define X86_ISNS_H

#include "backend_isn.h" /* MAX_OPERANDS */

struct x86_isn
{
	const char *mnemonic;

	enum operand_category constraints[MAX_OPERANDS];
};

extern const struct backend_isn
	x86_isn_mov,
	x86_isn_lea,
	x86_isn_add,
	x86_isn_cmp,
	x86_isn_imul,
	x86_isn_movzx,
	x86_isn_set,
	x86_isn_test;

#endif
