#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "function.h"
#include "function_internal.h"
#include "lbl.h"
#include "block_internal.h"
#include "blk_reg.h"

static void function_add_block(function *, block *);

struct function
{
	char *name;
	block *entry, *exit;

	unsigned *uniq_counter;

	block **blocks;
	size_t nblocks;

	unsigned retsz;
};

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
	printf("%u %s()", f->retsz, f->name);

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
