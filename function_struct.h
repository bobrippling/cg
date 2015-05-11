#ifndef FUNCTION_STRUCT_H
#define FUNCTION_STRUCT_H

struct function
{
	char *name;
	block *entry, *exit;

	unsigned *uniq_counter;

	block **blocks;
	size_t nblocks;

	variable *args;
	size_t nargs;

	unsigned retsz;
};

#endif
