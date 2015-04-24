#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "function.h"
#include "lbl.h"
#include "block_internal.h"

static void function_add_block(function *, block *);

struct function
{
	char *name;
	block *entry, *exit;
	block *trash; /* used for holding unused / error isns */

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
	fn->entry = block_new_entry();
	fn->uniq_counter = uniq_counter;

	function_add_block(fn, fn->entry);

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

block *function_entry_block(function *f)
{
	return f->entry;
}

static block *ondemand(block **p)
{
	if(!*p)
		*p = block_new(NULL);

	return *p;
}

block *function_exit_block(function *f)
{
	if(!f->exit)
		f->exit = block_new(lbl_new(f->uniq_counter));

	return f->exit;
}

block *function_block_trash(function *f)
{
	return ondemand(&f->trash);
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

void function_dump(function *f)
{
	printf("%u %s()\n{\n", f->retsz, f->name);

	block_dump(f->entry);

	printf("}\n");
}

const char *function_name(function *f)
{
	return f->name;
}
