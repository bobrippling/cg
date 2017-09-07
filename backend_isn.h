#ifndef BACKEND_ISN_H
#define BACKEND_ISN_H

#include <stdbool.h>

enum operand_category
{
	/* 0 means no entry / end of entries */
	OPERAND_REG = 1, /* %rax */
	OPERAND_MEM_PTR, /* %rsp +/- ... */
	OPERAND_MEM_CONTENTS, /* +/-...(%rbp), _label, 5 */
	OPERAND_INT, /* $5 */
	OPERAND_IMPLICIT,

	OPERAND_INPUT = 1 << 3,
	OPERAND_OUTPUT = 1 << 4,
	OPERAND_ADDRESSED = 1 << 5
};
#define OPERAND_MASK_PLAIN 0x7

#define MAX_OPERANDS 3
#define MAX_ISN_COMBOS 6

struct backend_isn
{
	const char *mnemonic;

	struct backend_isn_constraint {
		enum operand_category category[MAX_OPERANDS];
	} constraints[MAX_ISN_COMBOS];

	/* FIXME: x86 specific */
	bool may_suffix;
};

const char *operand_category_to_str(enum operand_category);

#endif
