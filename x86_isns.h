#ifndef X86_ISNS_H
#define X86_ISNS_H

struct x86_isn
{
	const char *mnemonic;

	enum operand_category constraints[MAX_OPERANDS];
};

#endif
