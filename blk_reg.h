#ifndef BLK_REG_H
#define BLK_REG_H

#include "block.h"

void blk_regalloc(
		block *,
		int nregs, int scratch_reg,
		const int *callee_save, unsigned callee_save_cnt);

void blk_lifecheck(block *);

#endif
