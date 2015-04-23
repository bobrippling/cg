#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "function.h"
#include "block_internal.h"

static void function_add_block(function *, block *);

struct function
{
	char *name;
	block *entry;

	block **blocks;
	size_t nblocks;

	unsigned retsz;
};

function *function_new(const char *lbl, unsigned retsz)
{
	function *fn = xcalloc(1, sizeof *fn);

	fn->name = xstrdup(lbl);
	fn->retsz = retsz;
	fn->entry = block_new_entry();

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

block *function_block_n(function *f, size_t n)
{
	return n >= f->nblocks ? NULL : f->blocks[n];
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
