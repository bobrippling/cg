#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "function.h"
#include "function_internal.h"
#include "lbl.h"
#include "block_internal.h"
#include "regalloc_blk.h"
#include "variable.h"
#include "variable_struct.h"
#include "function_struct.h"

static void function_add_block(function *, block *);

function *function_new(
		const char *lbl, unsigned retsz,
		unsigned *uniq_counter)
{
	function *fn = xcalloc(1, sizeof *fn);

	fn->name = xstrdup(lbl);
	fn->retsz = retsz;
	fn->uniq_counter = uniq_counter;

	return fn;
}

void function_free(function *f)
{
	function_onblocks(f, block_free);
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

block *function_entry_block(function *f, bool create)
{
	if(!f->entry && create){
		f->entry = block_new_entry();
		function_add_block(f, f->entry);
	}

	return f->entry;
}

block *function_exit_block(function *f)
{
	if(!f->exit)
		f->exit = function_block_new(f);

	return f->exit;
}

block *function_block_n(function *f, size_t n)
{
	return n >= f->nblocks ? NULL : f->blocks[n];
}

block *function_block_new(function *f)
{
	block *b = block_new(lbl_new(f->uniq_counter));
	function_add_block(f, b);
	return b;
}

block *function_block_find(
		function *f,
		char *ident /*takes ownership*/,
		int *const created)
{
	size_t i;
	block *b;

	for(i = 0; i < f->nblocks; i++){
		const char *lbl;

		b = f->blocks[i];
		lbl = block_label(b);

		if(lbl && !strcmp(ident, lbl)){
			if(created)
				*created = 0;

			free(ident);
			return b;
		}
	}

	if(created)
		*created = 1;

	b = block_new(ident);
	function_add_block(f, b);
	return b;
}

void function_finalize(function *f)
{
	function_onblocks(f, block_finalize);

	if(f->entry)
		blk_lifecheck(f->entry);
}

void function_dump(function *f)
{
	size_t i;

	printf("%u %s(", f->retsz, f->name);
	for(i = 0; i < f->nargs; i++)
		variable_dump(&f->args[i].var, i == f->nargs - 1 ? "" : ", ");

	printf(")");

	if(f->entry){
		printf("\n{\n");

		block_dump(f->entry);

		printf("}");
	}else{
		printf(";");
	}

	printf("\n");
}

const char *function_name(function *f)
{
	return f->name;
}

void function_arg_add(function *f, unsigned sz, char *name)
{
	f->nargs++;
	f->args = xrealloc(f->args, f->nargs * sizeof *f->args);

	f->args[f->nargs - 1].var.sz = sz;
	f->args[f->nargs - 1].var.name = name;

	memset(
			&f->args[f->nargs - 1].val,
			0,
			sizeof f->args[f->nargs - 1].val);
}

variable *function_arg_find(function *f, const char *name, size_t *const idx)
{
	size_t i;
	for(i = 0; i < f->nargs; i++){
		if(!strcmp(name, f->args[i].var.name)){
			*idx = i;
			return &f->args[i].var;
		}
	}

	return NULL;
}
