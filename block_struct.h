#ifndef BLOCK_STRUCT_H
#define BLOCK_STRUCT_H

#include "isn_internal.h"

struct block
{
	isn *isn1, **isntail; /* isntail = &isn1 initially */
	int is_entry;
};

#endif
