#ifndef X86_ISNS_H
#define X86_ISNS_H

#include <stdbool.h>

typedef enum operand_category
{
	/* 0 means no entry / end of entries */
	OPERAND_REG = 1, /* %rax */
	OPERAND_MEM_PTR, /* %rsp +/- ... */
	OPERAND_MEM_CONTENTS, /* +/-...(%rbp), _label, 5 */
	OPERAND_INT /* $5 */
} operand_category;

#define MAX_OPERANDS 3
#define MAX_ISN_COMBOS 6

struct x86_isn
{
	const char *mnemonic;

	unsigned arg_count;
	bool may_suffix;

	enum {
		OPERAND_INPUT = 1 << 0,
		OPERAND_OUTPUT = 1 << 1,
		OPERAND_LEA = 1 << 2
	} arg_ios[MAX_OPERANDS];

	struct x86_isn_constraint
	{
		operand_category category[MAX_OPERANDS];
	} constraints[MAX_ISN_COMBOS];
};

typedef struct emit_isn_operand {
	struct val *val;
	bool dereference;
} emit_isn_operand;

struct x86_octx;
void x86_emit_isn(
		const struct x86_isn *isn, struct x86_octx *octx,
		emit_isn_operand operands[],
		unsigned operand_count,
		const char *isn_suffix);

extern const struct x86_isn x86_isn_mov;
extern const struct x86_isn x86_isn_lea;
extern const struct x86_isn x86_isn_movzx;
extern const struct x86_isn x86_isn_add;
extern const struct x86_isn x86_isn_imul;
extern const struct x86_isn x86_isn_cmp;
extern const struct x86_isn x86_isn_test;
extern const struct x86_isn x86_isn_call;
extern const struct x86_isn x86_isn_set;

#endif
