#include "backend_isn.h"

#include "macros.h"
#include "isn_struct.h"
#include "x86.h"
#include "x86_isel.h"

#define X86_CONSTRAINT(l, r) { \
	l | OPERAND_INPUT, \
	r | OPERAND_INPUT | OPERAND_OUTPUT \
}

#define op x86_isn_add
#define call x86_isn_call
#define cmp x86_isn_cmp
#define imul3 x86_isn_imul
#define lea x86_isn_lea
#define mov x86_isn_mov
#define movzx x86_isn_movzx
#define set x86_isn_set
#define test x86_isn_test
#define static

static const struct backend_isn mov = {
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

static const struct backend_isn lea = {
	"lea",
	{
		{
			OPERAND_INPUT | OPERAND_ADDRESSED | OPERAND_MEM_PTR,
			OPERAND_OUTPUT | OPERAND_REG
		},
	},
	false
};

static const struct backend_isn movzx = {
	"mov", /* movzx */
	{
		{ OPERAND_INPUT | OPERAND_REG,          OPERAND_OUTPUT | OPERAND_REG },
		{ OPERAND_INPUT | OPERAND_REG,          OPERAND_OUTPUT | OPERAND_MEM_CONTENTS },
		{ OPERAND_INPUT | OPERAND_MEM_CONTENTS, OPERAND_OUTPUT | OPERAND_REG },
	},
	false
};

static const struct backend_isn op = {
	"add",
	{
		X86_CONSTRAINT(OPERAND_REG,          OPERAND_REG),
		X86_CONSTRAINT(OPERAND_REG,          OPERAND_MEM_CONTENTS),
		X86_CONSTRAINT(OPERAND_MEM_CONTENTS, OPERAND_REG),
		X86_CONSTRAINT(OPERAND_INT,          OPERAND_REG),
		{ OPERAND_INPUT | OPERAND_REG, OPERAND_INPUT | OPERAND_INT, OPERAND_OUTPUT | OPERAND_REG },
		X86_CONSTRAINT(OPERAND_INT,          OPERAND_MEM_CONTENTS)
	},
	true
};

static const struct backend_isn imul3 = {
	"imul",
	{
		/* imul{bwl} imm[16|32], r/m[16|32], reg[16|32] */
		{ OPERAND_INT | OPERAND_INPUT, OPERAND_REG          | OPERAND_INPUT, OPERAND_REG | OPERAND_OUTPUT },
		{ OPERAND_INT | OPERAND_INPUT, OPERAND_MEM_CONTENTS | OPERAND_INPUT, OPERAND_REG | OPERAND_OUTPUT }
	},
	true
};

static const struct backend_isn cmp = {
	"cmp",
	{
		{ OPERAND_INPUT | OPERAND_REG,          OPERAND_INPUT | OPERAND_REG,          OPERAND_OUTPUT | OPERAND_IMPLICIT },
		{ OPERAND_INPUT | OPERAND_REG,          OPERAND_INPUT | OPERAND_MEM_CONTENTS, OPERAND_OUTPUT | OPERAND_IMPLICIT },
		{ OPERAND_INPUT | OPERAND_MEM_CONTENTS, OPERAND_INPUT | OPERAND_REG,          OPERAND_OUTPUT | OPERAND_IMPLICIT },
		{ OPERAND_INPUT | OPERAND_INT,          OPERAND_INPUT | OPERAND_REG,          OPERAND_OUTPUT | OPERAND_IMPLICIT },
		{ OPERAND_INPUT | OPERAND_INT,          OPERAND_INPUT | OPERAND_MEM_CONTENTS, OPERAND_OUTPUT | OPERAND_IMPLICIT },
	},
	true
};

static const struct backend_isn test = {
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

static const struct backend_isn call = {
	"call",
	{
		{ OPERAND_INPUT | OPERAND_REG,          OPERAND_OUTPUT | OPERAND_IMPLICIT },
		{ OPERAND_INPUT | OPERAND_MEM_CONTENTS, OPERAND_OUTPUT | OPERAND_IMPLICIT },
		{ OPERAND_INPUT | OPERAND_MEM_PTR,      OPERAND_OUTPUT | OPERAND_IMPLICIT },
	},
	false
};

static const struct backend_isn set = {
	"set",
	{
		{ OPERAND_OUTPUT | OPERAND_REG },
		{ OPERAND_OUTPUT | OPERAND_MEM_CONTENTS }, /* 1-byte */
	},
	false
};

const struct target_arch_isn backend_isns_x64[] = {
	/*ISN_LOAD*/         { &mov, NULL },
	/*ISN_STORE*/        { &mov, NULL },
	/*ISN_ALLOCA*/       { NULL, NULL },
	/*ISN_OP*/           { &op, NULL },
	/*ISN_CMP*/          { &cmp, NULL },
	/*ISN_ELEM*/         { &lea, &x86_isel_lea },
	/*ISN_PTRADD*/       { &op, NULL },
	/*ISN_PTRSUB*/       { &op, NULL },
	/*ISN_COPY*/         { &mov, NULL },
	/*ISN_EXT_TRUNC*/    { &movzx, NULL }, /* TODO */
	/*ISN_PTR2INT*/      { &mov, NULL },
	/*ISN_INT2PTR*/      { &mov, NULL },
	/*ISN_PTRCAST*/      { &mov, NULL },
	/*ISN_BR*/           { NULL, NULL },
	/*ISN_JMP*/          { NULL, NULL },
	/*ISN_RET*/          { NULL, NULL },
	/*ISN_CALL*/         { &call, NULL },
	/*ISN_IMPLICIT_USE*/ { NULL, NULL },
};

static_assert(countof(backend_isns_x64) == ISN_TYPE_COUNT, check_isn_count);
