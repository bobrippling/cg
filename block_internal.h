#ifndef BLOCK_INTERNAL_H
#define BLOCK_INTERNAL_H

#include "block.h"
#include "isn_internal.h"

isn *block_first_isn(block *);
void block_add_isn(block *, isn *);

#endif
