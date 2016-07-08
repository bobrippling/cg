#ifndef BLOCK_INTERNAL_H
#define BLOCK_INTERNAL_H

#include "block.h"

block *block_new_entry(void);
block *block_new(char *lbl /* consumed */);

const char *block_label(block *);

void block_add_pred(block *, block *);

#endif
