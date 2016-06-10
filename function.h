#ifndef FUNCTION_H
#define FUNCTION_H

#include <stdbool.h>
#include "block.h"
#include "variable.h"
#include "function_attributes.h"

typedef struct function function;
struct unit;

void function_free(function *);

const char *function_name(function *);
struct type *function_type(function *);
void function_dump_args_and_block(function *);
void function_dump(function *);

void function_finalize(function *);

void function_add_attributes(function *, enum function_attributes);
enum function_attributes function_attributes(function *);

void function_onblocks(function *, void (block *));

block *function_entry_block(function *, bool create);
block *function_exit_block(function *, struct unit *unit);

block *function_block_find(
		function *f,
		struct unit *unit,
		char *ident /*takes ownership*/,
		int *const created);

block *function_block_new(function *f, struct unit *unit);

bool function_arg_find(
		function *f, const char *name,
		size_t *const idx, struct type **const ty);

struct dynarray *function_arg_names(function *);
struct name_loc *function_arg_loc(function *, size_t idx);
size_t function_arg_count(function *);

struct regalloc_info;
void func_regalloc(function *f, struct regalloc_info *, unsigned *alloca);

bool function_is_forward_decl(function *);

#endif
