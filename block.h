#ifndef BLOCK_H
#define BLOCK_H

#include <stdbool.h>

struct val;
struct isn;

typedef struct block block;

bool block_unknown_ending(block *);

void block_free(block *);

int block_tenative(block *);

bool *block_flag(block *);
void blocks_clear_flags(block *);

void blocks_traverse(block *, void (block *, void *), void *);
struct isn *block_first_isn(block *);

#ifdef DYNMAP_H
dynmap *block_lifetime_map(block *);
#endif

void block_dump(block *);

void block_lifecheck(block *);

#endif
