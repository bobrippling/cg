#ifndef ISN_REG_H
#define ISN_REG_H

#include "isn_internal.h"
#include "block.h"

struct regalloc_ctx
{
	int nregs, scratch_reg;
};

void isn_regalloc(block *blk, const struct regalloc_ctx *);

#endif
