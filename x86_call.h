#ifndef X86_CALL_H
#define X86_CALL_H

void x86_emit_call(
		block *blk, unsigned isn_idx,
		val *into_or_null, struct val *fn,
		dynarray *args,
		x86_octx *octx);

#endif
