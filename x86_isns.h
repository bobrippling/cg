#ifndef X86_ISNS_H
#define X86_ISNS_H

#include <stdbool.h>

typedef struct emit_isn_operand {
	struct val *val;
	bool dereference;
} emit_isn_operand;

struct x86_octx;
struct backend_isn;
void x86_emit_isn(
		const struct backend_isn *isn, struct x86_octx *octx,
		emit_isn_operand operands[],
		unsigned operand_count,
		const char *isn_suffix);

#endif
