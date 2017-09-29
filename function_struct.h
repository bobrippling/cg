#ifndef FUNCTION_STRUCT_H
#define FUNCTION_STRUCT_H

#include "variable_struct.h"

#include "val.h"
#include "val_struct.h"
#include "function_attributes.h"

struct function
{
	char *name;
	block *entry, *exit;

	unsigned *uniq_counter;

	block **blocks;
	size_t nblocks;

	type *fnty;
	dynarray arg_names;
	dynarray arg_locns;
	val **arg_vals;

	unsigned stackspace;

	enum function_attributes attr;
};

#endif
