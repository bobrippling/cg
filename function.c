#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "mem.h"
#include "function.h"
#include "function_internal.h"
#include "lbl.h"
#include "block_internal.h"
#include "variable.h"
#include "variable_struct.h"
#include "function_struct.h"
#include "isn_struct.h"
#include "regalloc.h"
#include "unit_internal.h"

static void function_add_block(function *, block *);

function *function_new(
		char *lbl, struct type *fnty,
		dynarray *toplvl_args,
		unsigned *uniq_counter)
{
	function *fn = xcalloc(1, sizeof *fn);

	fn->name = lbl;
	fn->fnty = fnty;
	fn->uniq_counter = uniq_counter;

	dynarray_init(&fn->arg_names);
	dynarray_move(&fn->arg_names, toplvl_args);

	dynarray_init(&fn->arg_locns);

	return fn;
}

void function_free(function *f)
{
	function_onblocks(f, block_free);

	dynarray_foreach(&f->arg_names, free);
	dynarray_reset(&f->arg_names);

	dynarray_foreach(&f->arg_locns, free);
	dynarray_reset(&f->arg_locns);

	free(f->blocks);
	free(f->name);
	free(f);
}

void function_add_block(function *f, block *b)
{
	f->nblocks++;
	f->blocks = xrealloc(f->blocks, f->nblocks * sizeof *f->blocks);
	f->blocks[f->nblocks - 1] = b;
}

void function_onblocks(function *f, void cb(block *))
{
	size_t i;
	for(i = 0; i < f->nblocks; i++)
		cb(f->blocks[i]);
}

dynarray *function_arg_names(function *f)
{
	return &f->arg_names;
}

struct name_loc *function_arg_loc(function *fn, size_t idx)
{
	assert(idx < dynarray_count(&fn->arg_locns));
	return dynarray_ent(&fn->arg_locns, idx);
}

block *function_entry_block(function *f, bool create)
{
	if(!f->entry && create){
		f->entry = block_new_entry();
		function_add_block(f, f->entry);
	}

	return f->entry;
}

block *function_exit_block(function *f, unit *unit)
{
	if(!f->exit)
		f->exit = function_block_new(f, unit);

	return f->exit;
}

block *function_block_new(function *f, unit *unit)
{
	block *b = block_new(lbl_new_private(f->uniq_counter, unit_lbl_private_prefix(unit)));
	function_add_block(f, b);
	return b;
}

block *function_block_find(
		function *f,
		unit *unit,
		char *ident /*takes ownership*/,
		int *const created)
{
	const char *prefix = unit_lbl_private_prefix(unit);
	size_t i;
	block *b;

	for(i = 0; i < f->nblocks; i++){
		const char *lbl;

		b = f->blocks[i];
		lbl = block_label(b);

		if(lbl && lbl_equal_to_ident(lbl, ident, prefix)){
			if(created)
				*created = 0;

			free(ident);
			return b;
		}
	}

	if(created)
		*created = 1;

	b = block_new(lbl_new_ident(ident, prefix));
	free(ident);
	function_add_block(f, b);
	return b;
}

void function_finalize(function *f)
{
	function_onblocks(f, block_finalize);

	if(f->entry)
		block_lifecheck(f->entry);
}

void function_add_attributes(function *f, enum function_attributes attr)
{
	f->attr |= attr;
}

enum function_attributes function_attributes(function *f)
{
	return f->attr;
}

static void print_func_and_args(dynarray *arg_tys, dynarray *arg_names, bool variadic)
{
	const size_t nargs = dynarray_count(arg_tys);
	size_t i;

	printf("(");

	dynarray_iter(arg_tys, i){
		variable tmpvar;

		tmpvar.ty = dynarray_ent(arg_tys, i);

		if(dynarray_is_empty(arg_names))
			tmpvar.name = "";
		else
			tmpvar.name = dynarray_ent(arg_names, i);

		printf("%s%s%s%s",
				type_to_str(tmpvar.ty),
				*tmpvar.name ? " $" : "",
				tmpvar.name,
				i == nargs - 1 ? "" : ", ");
	}

	if(variadic){
		printf("%s...", i > 0 ? ", " : "");
	}

	printf(")");
}

void function_dump_args_and_block(function *f)
{
	/* we are in charge of printing the full type attempt to print the normal
	 * type if possible (i.e. no "custom" arguments) */

	if(dynarray_is_empty(&f->arg_names)){
		printf("%s", type_to_str(f->fnty));
	}else{
		print_func_and_args(
				type_func_args(f->fnty),
				&f->arg_names,
				type_is_fn_variadic(f->fnty));
	}

	if(f->entry){
		printf("\n{\n");

		block_dump(f->entry);

		printf("}");
	}

	printf("\n");
}

const char *function_name(function *f)
{
	return f->name;
}

struct type *function_type(function *f)
{
	return f->fnty;
}

size_t function_arg_count(function *f)
{
	return dynarray_count(&f->arg_names);
}

bool function_arg_find(
		function *f, const char *name,
		size_t *const idx, type **const ty)
{
	size_t i;
	dynarray_iter(&f->arg_names, i){
		if(!strcmp(name, dynarray_ent(&f->arg_names, i))){
			dynarray *const arg_tys = type_func_args(f->fnty);
			*idx = i;
			*ty = dynarray_ent(arg_tys, i);
			return true;
		}
	}

	return false;
}

static struct name_loc *locate_arg_reg(
		size_t idx,
		const struct backend_traits *backend)
{
	struct name_loc *loc = xmalloc(sizeof *loc);

	if(idx >= backend->arg_regs_cnt){
		assert(0 && "TODO: arg on stack");
	}else{
		loc->where = NAME_IN_REG;
		loc->u.reg = backend->arg_regs[idx];
	}

	return loc;
}

static void assign_argument_registers(
		function *f,
		const struct backend_traits *backend)
{
	size_t i;

	dynarray_iter(&f->arg_names, i){
		struct name_loc *arg_loc = locate_arg_reg(i, backend);

		dynarray_add(&f->arg_locns, arg_loc);
	}
}

void func_regalloc(
		function *f,
		struct regalloc_info *info,
		unsigned *const alloca)
{
	block *const entry = function_entry_block(f, false);

	assign_argument_registers(f, &info->backend);

	regalloc(entry, info, alloca);
}

bool function_is_forward_decl(function *f)
{
	return !function_entry_block(f, false);
}
