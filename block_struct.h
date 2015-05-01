#ifndef BLOCK_STRUCT_H
#define BLOCK_STRUCT_H

#include "isn_internal.h"
#include "lifetime_struct.h"

struct block
{
	isn *isn1, **isntail; /* isntail = &isn1 initially */
	char *lbl; /* NULL if entry block */

	struct dynmap *val_lifetimes; /* val => struct lifetime */

	enum block_type
	{
		BLK_UNKNOWN,
		BLK_ENTRY,
		BLK_EXIT,
		BLK_BRANCH,
		BLK_JMP
	} type;

	union
	{
		struct
		{
			val *cond;
			block *t, *f;
		} branch;
		struct
		{
			block *target;
		} jmp;
	} u;

	unsigned emitted;
};

void block_set_type(block *blk, enum block_type type);

#endif
