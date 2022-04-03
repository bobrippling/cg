#ifndef ISEL_H
#define ISEL_H

struct selected_operand
{
	enum selected_operand_type {
		OP_IMMEDIATE,
		OP_REG,
		OP_STACK
	} type;

	union {
		int immediate;
		regt reg;
		int stackoff;
	} u;
};

struct selected_isn
{
	const char *mnemonic;

	struct selected_operand operands[2];
	int nops; /* 0 - 2 */
};

#endif
