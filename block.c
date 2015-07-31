#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "mem.h"
#include "dynmap.h"

#include "block.h"
#include "block_struct.h"
#include "block_internal.h"

#include "isn_struct.h"

#include "val.h"
#include "val_internal.h"
#include "val_struct.h"

block *block_new(char *lbl)
{
	block *b = xcalloc(1, sizeof *b);
	b->isntail = &b->isn1;
	b->lbl = lbl;

	b->val_lifetimes = dynmap_new(val *, NULL, val_hash);

	return b;
}

static void branch_free(block *b)
{
	val_release(b->u.branch.cond);
	dynarray_reset(&b->preds);
}

void block_free(block *b)
{
	size_t i;
	struct lifetime *lt;

	if(b->type == BLK_BRANCH)
		branch_free(b);

	for(i = 0; (lt = dynmap_value(struct lifetime *, b->val_lifetimes, i)); i++)
		free(lt);

	dynmap_free(b->val_lifetimes);

	isn_free_r(b->isn1);
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
	return b->isn1;
}

int block_tenative(block *b)
{
	return block_first_isn(b) == NULL;
}

void block_add_isn(block *blk, isn *isn)
{
	*blk->isntail = isn;
	blk->isntail = &isn->next;
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

bool *block_flag(block *blk)
{
	return &blk->flag_user;
}

void blocks_traverse(block *blk, void fn(block *, void *), void *ctx)
{
	bool *const flag = &blk->flag_iter;

	if(*flag)
		return;
	*flag = true;

	fn(blk, ctx);

	switch(blk->type){
		case BLK_UNKNOWN:
			assert(0 && "unknown block");
		case BLK_ENTRY:
		case BLK_EXIT:
			break;
		case BLK_BRANCH:
			blocks_traverse(blk->u.branch.t, fn, ctx);
			blocks_traverse(blk->u.branch.f, fn, ctx);
			break;
		case BLK_JMP:
			blocks_traverse(blk->u.jmp.target, fn, ctx);
			break;
	}

	*flag = false;
}

static void clear_flag(block *blk, void *ctx)
{
	(void)ctx;
	blk->flag_user = false;
}

void blocks_clear_flags(block *blk)
{
	blocks_traverse(blk, clear_flag, NULL);
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
