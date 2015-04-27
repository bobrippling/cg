#ifndef BLOCK_H
#define BLOCK_H

#include <stdbool.h>

struct val;
struct isn;

typedef struct block block;

bool block_unknown_ending(block *);

void block_free(block *);

int block_tenative(block *);

void blocks_iterate(block *, void (block *, void *), void *);
struct isn *block_first_isn(block *);

void block_dump(block *);

#endif
