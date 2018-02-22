#include "backend_isn.h"

#include "macros.h"
#include "isn_struct.h"
#include "x86.h"
#include "x86_isel.h"

const struct backend_isn x86_isn_mov = {
	"mov",
	{
		{ OPERAND_INPUT | OPERAND_REG,          OPERAND_OUTPUT | OPERAND_REG },
		{ OPERAND_INPUT | OPERAND_MEM_CONTENTS, OPERAND_OUTPUT | OPERAND_REG },
		{ OPERAND_INPUT | OPERAND_INT,          OPERAND_OUTPUT | OPERAND_REG },

		{ OPERAND_INPUT | OPERAND_REG,          OPERAND_INPUT | OPERAND_MEM_CONTENTS },
		{ OPERAND_INPUT | OPERAND_REG,          OPERAND_INPUT | OPERAND_INT },
		{ OPERAND_INPUT | OPERAND_INT,          OPERAND_INPUT | OPERAND_MEM_CONTENTS }
	},
	true
};

const struct backend_isn x86_isn_lea = {
	"lea",
	{
		{
			OPERAND_INPUT | OPERAND_ADDRESSED | OPERAND_MEM_PTR,
			OPERAND_OUTPUT | OPERAND_REG
		},
	},
	false
};

const struct backend_isn x86_isn_movzx = {
	"mov", /* movzx */
	{
		{ OPERAND_INPUT | OPERAND_REG,          OPERAND_OUTPUT | OPERAND_REG },
		{ OPERAND_INPUT | OPERAND_REG,          OPERAND_OUTPUT | OPERAND_MEM_CONTENTS },
		{ OPERAND_INPUT | OPERAND_MEM_CONTENTS, OPERAND_OUTPUT | OPERAND_REG },
	},
	false
};

const struct backend_isn x86_isn_add = {
	"add",
	{
		{ OPERAND_OUTPUT | OPERAND_INPUT | OPERAND_REG,               OPERAND_INPUT | OPERAND_REG          },
		{ OPERAND_OUTPUT | OPERAND_INPUT | OPERAND_MEM_CONTENTS,      OPERAND_INPUT | OPERAND_REG          },
		{ OPERAND_OUTPUT | OPERAND_INPUT | OPERAND_REG,               OPERAND_INPUT | OPERAND_MEM_CONTENTS },
		{ OPERAND_OUTPUT | OPERAND_INPUT | OPERAND_REG,               OPERAND_INPUT | OPERAND_INT          },
		{ OPERAND_OUTPUT | OPERAND_INPUT | OPERAND_MEM_CONTENTS,      OPERAND_INPUT | OPERAND_INT          },

		{ OPERAND_INPUT | OPERAND_REG, OPERAND_INPUT  | OPERAND_INT, OPERAND_OUTPUT | OPERAND_REG },
	},
	true
};

const struct backend_isn x86_isn_imul = {
	"imul",
	{
		/* imul{bwl} imm[16|32], r/m[16|32], reg[16|32] */
		{ OPERAND_INT | OPERAND_INPUT, OPERAND_REG          | OPERAND_INPUT, OPERAND_REG | OPERAND_OUTPUT },
		{ OPERAND_INT | OPERAND_INPUT, OPERAND_MEM_CONTENTS | OPERAND_INPUT, OPERAND_REG | OPERAND_OUTPUT }
	},
	true
};

const struct backend_isn x86_isn_cmp = {
	"cmp",
	{
		{ OPERAND_INPUT | OPERAND_REG,          OPERAND_INPUT | OPERAND_REG,          OPERAND_OUTPUT | OPERAND_IMPLICIT },
		{ OPERAND_INPUT | OPERAND_MEM_CONTENTS, OPERAND_INPUT | OPERAND_REG,          OPERAND_OUTPUT | OPERAND_IMPLICIT },
		{ OPERAND_INPUT | OPERAND_REG,          OPERAND_INPUT | OPERAND_MEM_CONTENTS, OPERAND_OUTPUT | OPERAND_IMPLICIT },
		{ OPERAND_INPUT | OPERAND_REG,          OPERAND_INPUT | OPERAND_INT,          OPERAND_OUTPUT | OPERAND_IMPLICIT },
		{ OPERAND_INPUT | OPERAND_MEM_CONTENTS, OPERAND_INPUT | OPERAND_INT,          OPERAND_OUTPUT | OPERAND_IMPLICIT },
	},
	true
};

const struct backend_isn x86_isn_test = {
	"test",
	{
		{ OPERAND_INPUT | OPERAND_REG,          OPERAND_INPUT | OPERAND_REG },
		{ OPERAND_INPUT | OPERAND_REG,          OPERAND_INPUT | OPERAND_MEM_CONTENTS },
		{ OPERAND_INPUT | OPERAND_MEM_CONTENTS, OPERAND_INPUT | OPERAND_REG },
		{ OPERAND_INPUT | OPERAND_INT,          OPERAND_INPUT | OPERAND_REG },
		{ OPERAND_INPUT | OPERAND_INT,          OPERAND_INPUT | OPERAND_MEM_CONTENTS },
	},
	true
};

const struct backend_isn x86_isn_call = {
	"call",
	{
		{ OPERAND_INPUT | OPERAND_REG,          OPERAND_OUTPUT | OPERAND_IMPLICIT },
		{ OPERAND_INPUT | OPERAND_MEM_CONTENTS, OPERAND_OUTPUT | OPERAND_IMPLICIT },
		{ OPERAND_INPUT | OPERAND_MEM_PTR,      OPERAND_OUTPUT | OPERAND_IMPLICIT },
	},
	false
};

const struct backend_isn x86_isn_set = {
	"set",
	{
		{ OPERAND_OUTPUT | OPERAND_REG },
		{ OPERAND_OUTPUT | OPERAND_MEM_CONTENTS }, /* 1-byte */
	},
	false
};

const struct backend_isn x86_isn_jmp = {
	"jmp",
	{
		{ OPERAND_INPUT | OPERAND_REG },
		{ OPERAND_INPUT | OPERAND_MEM_CONTENTS }
	},
	false
};

const struct target_arch_isn backend_isns_x64[] = {
	/*ISN_LOAD*/         { &x86_isn_mov, NULL },
	/*ISN_STORE*/        { &x86_isn_mov, NULL },
	/*ISN_ALLOCA*/       { NULL, NULL },
	/*ISN_OP*/           { &x86_isn_add, &x86_isel_op },
	/*ISN_CMP*/          { &x86_isn_cmp, NULL },
	/*ISN_ELEM*/         { &x86_isn_lea, &x86_isel_lea },
	/*ISN_PTRADD*/       { &x86_isn_add, NULL },
	/*ISN_PTRSUB*/       { &x86_isn_add, NULL },
	/*ISN_COPY*/         { &x86_isn_mov, NULL },
	/*ISN_MEMCPY*/       { NULL, NULL },
	/*ISN_EXT_TRUNC*/    { &x86_isn_movzx, NULL },
	/*ISN_PTR2INT*/      { &x86_isn_mov, NULL },
	/*ISN_INT2PTR*/      { &x86_isn_mov, NULL },
	/*ISN_PTRCAST*/      { &x86_isn_mov, NULL },
	/*ISN_BR*/           { NULL, NULL },
	/*ISN_JMP*/          { NULL, NULL },
	/*ISN_JMP_COMPUTED*/ { &x86_isn_jmp, NULL },
	/*ISN_LABEL*/        { NULL, NULL },
	/*ISN_RET*/          { NULL, NULL },
	/*ISN_CALL*/         { &x86_isn_call, NULL },
	/*ISN_ASM*/          { NULL, NULL },
	/*ISN_IMPLICIT_USE_START*/ { NULL, NULL },
	/*ISN_IMPLICIT_USE_END*/ { NULL, NULL },
};

static_assert(countof(backend_isns_x64) == ISN_TYPE_COUNT, check_isn_count);
