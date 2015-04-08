#ifndef BLOCK_H
#define BLOCK_H

typedef struct block block;

block *block_new(const char *lbl);

void block_free(block *);

int block_tenative(block *);

void blocks_iterate(block *, void (block *, void *), void *);

void block_dump(block *);

#endif
