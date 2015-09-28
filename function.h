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

bool function_arg_find(
		function *f, const char *name,
		size_t *const idx, struct type **const ty);

struct dynarray *function_arg_names(function *);
struct name_loc *function_arg_loc(function *, size_t idx);
size_t function_arg_count(function *);

struct regalloc_info;
void func_regalloc(function *f, struct regalloc_info *);

#endif
