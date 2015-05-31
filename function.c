#include <stdio.h>
#include <stdlib.h>

#include "mem.h"
#include "function.h"
#include "block_internal.h"

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

void function_dump(function *f)
{
	printf("%u %s()\n{\n", f->retsz, f->name);

	block_dump(f->entry);

	printf("}\n");
}
