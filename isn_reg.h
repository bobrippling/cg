#ifndef ISN_REG_H
#define ISN_REG_H

#include "isn_internal.h"
#include "block.h"

struct regalloc_ctx
{
	int nregs, scratch_reg;
	unsigned ptrsz;

	const int *callee_save;
	unsigned callee_save_cnt;
};

void isn_regalloc(block *blk, const struct regalloc_ctx *);

#endif
