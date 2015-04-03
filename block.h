#ifndef BLOCK_H
#define BLOCK_H

typedef struct block block;

block *block_new(void);
block *block_new_entry(void);

void block_free(block *);

void blocks_iterate(block *, void (block *, void *), void *);

void block_dump(block *);

#endif
