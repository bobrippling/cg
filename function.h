#ifndef FUNCTION_H
#define FUNCTION_H

#include <stdbool.h>
#include "block.h"
#include "variable.h"

typedef struct function function;

void function_free(function *);

const char *function_name(function *);
struct type *function_type(function *);
void function_dump_args_and_block(function *);

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

bool function_arg_find(
		function *f, const char *name,
		size_t *const idx, struct type **const ty);

struct dynarray *function_arg_names(function *);
void function_arg_vals(function *, struct dynarray *);

struct regalloc_context;
void func_regalloc(function *f, struct regalloc_context *ctx);

#endif
