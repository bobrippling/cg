#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "mem.h"
#include "dynmap.h"

#include "block.h"
#include "block_struct.h"
#include "block_internal.h"

#include "isn_struct.h"
#include "isn.h"

#include "val.h"
#include "val_internal.h"
#include "val_struct.h"

struct life_check_ctx
{
	block *blk;
	dynmap *values_to_block;
};

static void block_dump_r(block *);


block *block_new(char *lbl)
{
	block *b = xcalloc(1, sizeof *b);
	b->lbl = lbl;

	b->val_lifetimes = dynmap_new(val *, NULL, val_hash);

	return b;
}

static void branch_free(block *b)
{
	val_release(b->u.branch.cond);
}

static void block_rewind_isnhead(block *b)
{
	if(b->isnhead)
		b->isnhead = isn_first(b->isnhead);
}

void block_free(block *b)
{
	size_t i;
	struct lifetime *lt;

	if(b->type == BLK_BRANCH)
		branch_free(b);

	dynarray_reset(&b->preds);

	for(i = 0; (lt = dynmap_value(struct lifetime *, b->val_lifetimes, i)); i++)
		free(lt);

	dynmap_free(b->val_lifetimes);

	block_rewind_isnhead(b);
	isn_free_r(b->isnhead);
	free(b->lbl);
	free(b);
}

block *block_new_entry(void)
{
	block *b = block_new(NULL);
	return b;
}

dynmap *block_lifetime_map(block *b)
{
	return b->val_lifetimes;
}

const char *block_label(block *b)
{
	return b->lbl;
}

isn *block_first_isn(block *b)
{
	block_rewind_isnhead(b);
	return b->isnhead;
}

void block_add_isn(block *b, struct isn *i)
{
	if(!b->isnhead){
		b->isnhead = i;
		b->isntail = i;
		return;
	}

	assert(b->isntail);

	isn_insert_after(b->isntail, i);
	b->isntail = i;
}

int block_tenative(block *b)
{
	return block_first_isn(b) == NULL;
}

bool block_unknown_ending(block *blk)
{
	if(!blk)
		return false;

	return blk->type == BLK_UNKNOWN;
}

void block_set_type(block *blk, enum block_type type)
{
	assert(block_unknown_ending(blk));
	blk->type = type;
}

void block_set_branch(block *current, val *cond, block *btrue, block *bfalse)
{
	block_set_type(current, BLK_BRANCH);

	block_add_pred(btrue, current);
	block_add_pred(bfalse, current);

	current->u.branch.cond = val_retain(cond);
	current->u.branch.t = btrue;
	current->u.branch.f = bfalse;
}

void block_set_jmp(block *current, block *new)
{
	block_set_type(current, BLK_JMP);
	block_add_pred(new, current);
	current->u.jmp.target = new; /* weak ref */
}

static void blocks_traverse_r(
		block *blk,
		void fn(block *, void *),
		void *ctx,
		dynmap *markers)
{
	if(dynmap_get(block *, int, markers, blk))
		return;
	(void)dynmap_set(block *, int, markers, blk, 1);

	fn(blk, ctx);

	switch(blk->type){
		case BLK_UNKNOWN:
			assert(0 && "unknown block");
		case BLK_ENTRY:
		case BLK_EXIT:
			break;
		case BLK_BRANCH:
			blocks_traverse_r(blk->u.branch.t, fn, ctx, markers);
			blocks_traverse_r(blk->u.branch.f, fn, ctx, markers);
			break;
		case BLK_JMP:
			blocks_traverse_r(blk->u.jmp.target, fn, ctx, markers);
			break;
	}
}

void blocks_traverse(
		block *blk,
		void fn(block *, void *),
		void *ctx)
{
	dynmap *markers = BLOCK_DYNMAP_NEW();

	blocks_traverse_r(blk, fn, ctx, markers);

	dynmap_free(markers);
}

void block_add_pred(block *b, block *pred)
{
	dynarray_add(&b->preds, pred);
}

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
		isn_on_live_vals(isn, check_val_life_isn, &ctx_lifecheck);
}

void block_lifecheck(block *blk)
{
	/* find out which values live outside their block */
	dynmap *values_to_block = dynmap_new(val *, NULL, val_hash);

	blocks_traverse(blk, check_val_life_block, values_to_block);

	dynmap_free(values_to_block);
}

unsigned block_hash(block *b)
{
	return (b->lbl ? dynmap_strhash(b->lbl) : 0) ^ (unsigned)b;
}

static void block_dump1(block *blk)
{
	if(!dynarray_is_empty(&blk->preds)){
		size_t i;
		const char *comma = "";

		printf("# predecessors: ");

		dynarray_iter(&blk->preds, i){
			block *pred = dynarray_ent(&blk->preds, i);

			printf("%s%p", comma, pred);
			comma = ", ";
		}

		putchar('\n');
	}

	printf("# block: %p\n", blk);

	isn_dump(block_first_isn(blk), blk);
}

static void block_dump_lbl(block *blk)
{
	if(blk->emitted)
		return;

	blk->emitted = 1;

	printf("\n$%s:\n", blk->lbl);
	block_dump_r(blk);
}

static void block_dump_r(block *blk)
{
	block_dump1(blk);

	switch(blk->type){
		case BLK_UNKNOWN:
			assert(0 && "unknown block");
		case BLK_ENTRY:
		case BLK_EXIT:
			break;
		case BLK_BRANCH:
			block_dump_lbl(blk->u.branch.t);
			block_dump_lbl(blk->u.branch.f);
			break;
		case BLK_JMP:
			block_dump_lbl(blk->u.jmp.target);
			break;
	}
}

static void block_unmark_emitted(block *blk, void *vctx)
{
	blk->emitted = 0;
}

void block_dump(block *blk)
{
	block_dump_r(blk);
	blocks_traverse(blk, block_unmark_emitted, NULL);
}
