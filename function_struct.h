#ifndef FUNCTION_STRUCT_H
#define FUNCTION_STRUCT_H

#include "variable_struct.h"

#include "val.h"
#include "val_struct.h"

struct function
{
	char *name;
	block *entry, *exit;

	unsigned *uniq_counter;

	block **blocks;
	size_t nblocks;

	type *fnty;
	dynarray arg_names;
	dynarray arg_vals;
};

#endif
