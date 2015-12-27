#ifndef FUNCTION_H
#define FUNCTION_H

#include <stdbool.h>
#include "block.h"
#include "variable.h"

typedef struct function function;

void function_free(function *);

const char *function_name(function *);
void function_dump(function *);

void function_finalize(function *);

void function_onblocks(function *, void (block *));

block *function_entry_block(function *, bool create);
block *function_exit_block(function *);

block *function_block_find(
		function *f,
		char *ident /*takes ownership*/,
		int *const created);

block *function_block_new(function *f);

block *function_block_n(function *, size_t);

#define function_iter_blocks(fn, blk, i)    \
	for(i = 0, blk = function_block_n(fn, i); \
			blk;                                  \
			blk = function_block_n(fn, i))

variable *function_arg_find(function *, const char *, size_t *);

struct regalloc_context;
void func_regalloc(function *f, struct regalloc_context *ctx);

#endif
