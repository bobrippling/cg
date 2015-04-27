#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "mem.h"

#include "block.h"
#include "block_struct.h"
#include "block_internal.h"
#include "isn_struct.h"

block *block_new(char *lbl)
{
	block *b = xcalloc(1, sizeof *b);
	b->isntail = &b->isn1;
	b->lbl = lbl;
	return b;
}

static void branch_free(block *b)
{
	val_release(b->u.branch.cond);
}

void block_free(block *b)
{
	if(b->type == BLK_BRANCH)
		branch_free(b);

	isn_free_r(b->isn1);
	free(b->lbl);
	free(b);
}

block *block_new_entry(void)
{
	block *b = block_new(NULL);
	return b;
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
	assert(blk->type != BLK_BRANCH && "already branched - no more isns");

	*blk->isntail = isn;
	blk->isntail = &isn->next;
}

void block_set_type(block *blk, enum block_type type)
{
	assert(blk->type == BLK_UNKNOWN);
	blk->type = type;
}

void blocks_iterate(block *blk, void fn(block *, void *), void *ctx)
{
	fn(blk, ctx);

	switch(blk->type){
		case BLK_UNKNOWN:
			assert(0 && "unknown block");
		case BLK_ENTRY:
		case BLK_EXIT:
			break;
		case BLK_BRANCH:
			blocks_iterate(blk->u.branch.t, fn, ctx);
			blocks_iterate(blk->u.branch.f, fn, ctx);
			break;
		case BLK_JMP:
			blocks_iterate(blk->u.jmp.target, fn, ctx);
			break;
	}
}

static void block_dump1(block *blk)
{
	isn_dump(block_first_isn(blk));
}

static void block_dump_lbl(block *blk)
{
	printf("\n%s:\n", blk->lbl);
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
