#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "mem.h"

#include "block.h"
#include "block_struct.h"
#include "block_internal.h"
#include "isn_struct.h"
#include "branch_internal.h"

block *block_new(const char *lbl)
{
	block *b = xcalloc(1, sizeof *b);
	b->isntail = &b->isn1;
	b->lbl = lbl ? xstrdup(lbl) : NULL;
	return b;
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

isn *block_first_isn(block *b)
{
	return b->isn1;
}

void block_add_isn(block *blk, isn *isn)
{
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
	}
}

static void block_dump1(block *blk)
{
	isn_dump(block_first_isn(blk));
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
			printf("\tbr %s, %s, %s\n",
					val_str(blk->u.branch.cond),
					blk->u.branch.t->lbl,
					blk->u.branch.f->lbl);

			printf("\n%s:\n", blk->u.branch.t->lbl);
			block_dump(blk->u.branch.t);
			printf("\n%s:\n", blk->u.branch.f->lbl);
			block_dump(blk->u.branch.f);
	}
}
