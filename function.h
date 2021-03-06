#ifndef FUNCTION_H
#define FUNCTION_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "block.h"
#include "variable.h"
#include "function_attributes.h"

typedef struct function function;
struct unit;
struct target;

void function_free(function *);

const char *function_name(function *);
const char *function_name_mangled(function *, const struct target *);
struct type *function_type(function *);
void function_dump(function *, FILE *);

void function_finalize(function *);

void function_add_attributes(function *, enum function_attributes);
enum function_attributes function_attributes(function *);

void function_onblocks(function *, void (block *, void *), void *);
void function_blocks_traverse(function *, void (block *, void *), void *);

block *function_entry_block(function *, bool create);
block *function_exit_block(function *, struct unit *unit);

block *function_block_find(
		function *f,
		struct unit *unit,
		char *ident /*takes ownership*/,
		int *const created);

block *function_block_new(function *f, struct unit *unit);
void function_block_split(function *f, struct unit *unit, block *blk, struct isn *first_new, block **const newblk);

bool function_arg_find(
		function *f, const char *name,
		size_t *const idx, struct type **const ty);

void function_register_arg_val(function *, unsigned arg_idx, struct val *);
struct val *function_arg_val(function *, unsigned arg_idx);

struct dynarray *function_arg_names(function *);
size_t function_arg_count(function *);

unsigned function_alloc_stack_space(function *, struct type *for_ty);
unsigned function_get_stack_use(function *);

#if 0
struct regalloc_info;
void func_regalloc(function *f, struct regalloc_info *, unsigned *alloca);
#endif

bool function_is_forward_decl(function *);

#endif
