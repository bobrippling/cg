#ifndef FUNCTION_H
#define FUNCTION_H

#include "block.h"

typedef struct function function;

function *function_new(const char *lbl, unsigned retsz);
void function_free(function *);

void function_add_block(function *, block *);

void function_dump(function *);

void function_onblocks(function *, void (block *));

block *function_entry_block(function *);

block *function_block_n(function *, size_t);

#define function_iter_blocks(fn, blk, i)    \
	for(i = 0, blk = function_block_n(fn, i); \
			blk;                                  \
			blk = function_block_n(fn, i))

#endif
