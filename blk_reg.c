#include "blk_reg.h"

#include "isn_reg.h"
#include "block_internal.h"

static void blk_regalloc1(block *blk, void *ctx)
{
	const int nregs = *(int *)ctx;

	isn_regalloc(blk, nregs);
}

void blk_regalloc(block *blk, int nregs)
{
	blocks_iterate(blk, blk_regalloc1, &nregs);
}
