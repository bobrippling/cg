#include "x86_isns.h"

const struct x86_isn x86_isn_mov = {
	"mov",
	2,
	true,
	{
		OPERAND_INPUT,
		OPERAND_INPUT | OPERAND_OUTPUT,
		0
	},
	{
		{ OPERAND_REG, OPERAND_REG },
		{ OPERAND_REG, OPERAND_MEM },
		{ OPERAND_MEM, OPERAND_REG },
		{ OPERAND_REG, OPERAND_INT },
		{ OPERAND_INT, OPERAND_REG },
		{ OPERAND_INT, OPERAND_MEM }
	}
};

const struct x86_isn x86_isn_lea = {
	"lea",
	2,
	false,
	{
		OPERAND_INPUT | OPERAND_LEA,
		OPERAND_OUTPUT,
		0
	},
	{
		{ OPERAND_MEM, OPERAND_REG },
	}
};

const struct x86_isn x86_isn_movzx = {
	"mov",
	2,
	false,
	{
		OPERAND_INPUT,
		OPERAND_INPUT | OPERAND_OUTPUT,
		0
	},
	{
		{ OPERAND_REG, OPERAND_REG },
		{ OPERAND_REG, OPERAND_MEM },
	}
};

const struct x86_isn x86_isn_add = {
	"add",
	2,
	true,
	{
		OPERAND_INPUT,
		OPERAND_INPUT | OPERAND_OUTPUT,
		0
	},
	{
		{ OPERAND_REG, OPERAND_REG },
		{ OPERAND_REG, OPERAND_MEM },
		{ OPERAND_MEM, OPERAND_REG },
		{ OPERAND_INT, OPERAND_REG },
		{ OPERAND_INT, OPERAND_MEM }
	}
};

const struct x86_isn x86_isn_imul = {
	"imul",
	3,
	true,
	{
		OPERAND_INPUT,
		OPERAND_INPUT,
		OPERAND_OUTPUT
	},
	{
		/* imul{bwl} imm[16|32], r/m[16|32], reg[16|32] */
		{ OPERAND_INT, OPERAND_REG, OPERAND_REG },
		{ OPERAND_INT, OPERAND_MEM, OPERAND_REG }
	}
};

const struct x86_isn x86_isn_cmp = {
	"cmp",
	2,
	true,
	{
		OPERAND_INPUT,
		OPERAND_INPUT,
		0
	},
	{
		{ OPERAND_REG, OPERAND_REG },
		{ OPERAND_REG, OPERAND_MEM },
		{ OPERAND_MEM, OPERAND_REG },
		{ OPERAND_INT, OPERAND_REG },
		{ OPERAND_INT, OPERAND_MEM },
	}
};

const struct x86_isn x86_isn_test = {
	"test",
	2,
	true,
	{
		OPERAND_INPUT,
		OPERAND_INPUT,
		0
	},
	{
		{ OPERAND_REG, OPERAND_REG },
		{ OPERAND_REG, OPERAND_MEM },
		{ OPERAND_MEM, OPERAND_REG },
		{ OPERAND_INT, OPERAND_REG },
		{ OPERAND_INT, OPERAND_MEM },
	}
};

const struct x86_isn x86_isn_call = {
	"call",
	1,
	false,
	{
		OPERAND_INPUT,
		0,
		0
	},
	{
		{ OPERAND_REG },
		{ OPERAND_MEM },
	}
};

const struct x86_isn x86_isn_set = {
	"set",
	1,
	false,
	{
		OPERAND_OUTPUT,
		0,
		0
	},
	{
		{ OPERAND_REG },
		{ OPERAND_MEM }, /* 1-byte */
	}
};
