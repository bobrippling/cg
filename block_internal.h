#ifndef BLOCK_INTERNAL_H
#define BLOCK_INTERNAL_H

#include "block.h"
#include "isn_internal.h"

block *block_new_entry(void);

isn *block_first_isn(block *);
void block_add_isn(block *, isn *);

const char *block_label(block *);

#endif
