#ifndef X86_CALL_H
#define X86_CALL_H

extern const int x86_arg_regs[];
extern const unsigned x86_arg_reg_count;

void x86_emit_call(
		block *blk, unsigned isn_idx,
		val *into_or_null, struct val *fn,
		dynarray *args,
		x86_octx *octx);

#endif
