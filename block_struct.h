#ifndef BLOCK_STRUCT_H
#define BLOCK_STRUCT_H

#include <stdbool.h>

#include "lifetime_struct.h"
#include "dynarray.h"

struct block
{
	struct isn *isnhead, *isntail;
	char *lbl; /* NULL if entry block */

	struct dynmap *val_lifetimes; /* val => struct lifetime */
	dynarray preds;

	enum block_type type;

	bool flag_iter;

	union
	{
		struct
		{
			struct val *cond;
			block *t, *f;
		} branch;
		struct
		{
			block *target;
		} jmp;
	} u;

	unsigned emitted;
};

#endif
