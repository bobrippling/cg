#include "mem.h"

#include "block.h"
#include "block_struct.h"
#include "isn_struct.h"

block *block_new(void)
{
	block *b = xcalloc(1, sizeof *b);
	b->isntail = &b->isn1;
	return b;
}

block *block_new_entry(void)
{
	block *b = block_new();
	b->is_entry = 1;
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
