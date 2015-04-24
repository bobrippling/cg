#ifndef BLOCK_INTERNAL_H
#define BLOCK_INTERNAL_H

#include "block.h"
#include "isn_internal.h"

block *block_new_entry(void);
block *block_new(char *lbl /* consumed */);

void block_add_isn(block *, struct isn *);

const char *block_label(block *);

#endif
