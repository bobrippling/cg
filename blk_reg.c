#include <stddef.h>

#include "dynmap.h"

#include "blk_reg.h"

#include "isn_reg.h"
#include "val_struct.h"
#include "isn_struct.h"

struct life_check_ctx
{
	block *blk;
	dynmap *values_to_block;
};

static void check_val_life_isn(val *v, isn *isn, void *vctx)
{
	struct life_check_ctx *ctx = vctx;
	block *val_block;

	(void)isn;

	if(v->live_across_blocks)
		return; /* already confirmed */

	val_block = dynmap_get(val *, block *, ctx->values_to_block, v);
	if(val_block){
		/* value is alive in 'val_block' - if it's not the same as
		 * the current block then the value lives outside its block */

		if(val_block != ctx->blk){
			v->live_across_blocks = true;
		}else{
			/* encountered again in the same block. nothing to do */
		}
	}else{
		/* first encountering val. mark as alive in current block */
		dynmap_set(val *, block *, ctx->values_to_block, v, ctx->blk);
	}
}

static void check_val_life_block(block *blk, void *vctx)
{
	dynmap *values_to_block = vctx;
	struct life_check_ctx ctx_lifecheck;
	isn *isn;

	ctx_lifecheck.blk = blk;
	ctx_lifecheck.values_to_block = values_to_block;

	for(isn = block_first_isn(blk); isn; isn = isn->next)
		isn_on_vals(isn, check_val_life_isn, &ctx_lifecheck);
}

void blk_lifecheck(block *blk)
{
	/* find out which values live outside their block */
	dynmap *values_to_block = dynmap_new(val *, NULL, val_hash);

	blocks_iterate(blk, check_val_life_block, values_to_block);

	dynmap_free(values_to_block);
}

static void blk_regalloc_pass(block *blk, void *vctx)
{
	const struct regalloc_ctx *ctx = vctx;

	isn_regalloc(blk, ctx);
}

void blk_regalloc(
		block *blk,
		int nregs, int scratch_reg,
		const int *callee_save, unsigned callee_save_cnt)
{
	struct regalloc_ctx ctx_regalloc;
	ctx_regalloc.nregs = nregs;
	ctx_regalloc.scratch_reg = scratch_reg;

	ctx_regalloc.callee_save = callee_save;
	ctx_regalloc.callee_save_cnt = callee_save_cnt;

	blocks_iterate(blk, blk_regalloc_pass, &ctx_regalloc);
}
