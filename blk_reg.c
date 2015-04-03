#include "blk_reg.h"

#include "isn_reg.h"
#include "block_internal.h"

static void blk_regalloc1(block *blk, void *vctx)
{
	const struct regalloc_ctx *ctx = vctx;

	isn_regalloc(blk, ctx);
}

void blk_regalloc(block *blk, int nregs, int scratch_reg)
{
	struct regalloc_ctx ctx;

	ctx.nregs = nregs;
	ctx.scratch_reg = scratch_reg;

	blocks_iterate(blk, blk_regalloc1, &ctx);
}
