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
	return b->isnhead;
}

int block_tenative(block *b)
{
	return block_first_isn(b) == NULL;
}

void block_add_isn(block *blk, isn *isn)
{
	if(blk->isntail){
		assert(!blk->isntail->next);
		blk->isntail->next = isn;
		blk->isntail = isn;
		assert(!isn->next);
	}else{
		assert(!blk->isnhead);
		blk->isnhead = blk->isntail = isn;
	}
}

void block_insert_isn(block *blk, struct isn *isn)
{
	assert(!isn->next);
	isn->next = blk->isnhead;
	blk->isnhead = isn;
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
	bool *const flag = &blk->flag_iter;

	if(markers){
		if(dynmap_get(block *, int, markers, blk))
			return;
		(void)dynmap_set(block *, int, markers, blk, 1);
	}else{
		if(*flag)
			return;
		*flag = true;
	}

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

	if(markers)
		(void)dynmap_rm(block *, int, markers, blk);
	else
		*flag = false;
}

void blocks_traverse(
		block *blk,
		void fn(block *, void *),
		void *ctx,
		dynmap *markers)
{
	assert(!markers || dynmap_is_empty(markers));
	blocks_traverse_r(blk, fn, ctx, markers);
	assert(!markers || dynmap_is_empty(markers));
}

struct lifetime_assign_ctx
{
	unsigned isn_count;
	block *blk;
};

static void assign_lifetime(val *v, isn *isn, void *vctx)
{
	struct lifetime_assign_ctx *ctx = vctx;
	struct lifetime *lt;
	unsigned start;

	(void)isn;

	switch(v->kind){
		case FROM_ISN:
			start = ctx->isn_count;
			break;
		case ARGUMENT:
			start = 0;
			break;
		default:
			return;
	}

	lt = dynmap_get(val *, struct lifetime *, ctx->blk->val_lifetimes, v);

	if(!lt){
		lt = xcalloc(1, sizeof *lt);
		dynmap_set(val *, struct lifetime *, ctx->blk->val_lifetimes, v, lt);

		lt->start = start;
	}

	lt->end = ctx->isn_count;
}

static void assign_lifetimes(block *const blk, isn *const head)
{
	struct lifetime_assign_ctx ctx = { 0 };
	isn *i;

	ctx.blk = blk;

	for(i = head; i; i = i->next, ctx.isn_count++)
		isn_on_live_vals(i, assign_lifetime, &ctx);
}

void block_finalize(block *blk)
{
	assign_lifetimes(blk, block_first_isn(blk));
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

	/* if it's an argument, and used
	 * (which it is, as we're in a on_live_vals() call),
	 * then we mark it as inter-block, as:
	 * $f = i4(i4 $arg){
	 *    ...
	 *    br $cond, $a, $b
	 * $b:
	 *    call $somewhere()
	 *    ret $arg
	 *    ...
	 * }
	 *
	 * even though $arg is only used in one block,
	 * it must live through potentially the $a block
	 * in order to be valid upon arrival in $b.
	 */
	if(v->kind == ARGUMENT){
		v->live_across_blocks = true;
		return;
	}

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

	blocks_traverse(blk, check_val_life_block, values_to_block, NULL);

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
	block_dump(blk);
}

void block_dump(block *blk)
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
