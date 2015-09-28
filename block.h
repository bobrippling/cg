#ifndef BLOCK_H
#define BLOCK_H

#include <stdbool.h>
#include "macros.h"

struct val;
struct isn;
struct dynmap;

typedef struct block block;

bool block_unknown_ending(block *);

void block_free(block *);

int block_tenative(block *);

void blocks_traverse(block *, void (block *, void *), void *, struct dynmap *);

struct isn *block_first_isn(block *);

#ifdef DYNMAP_H
dynmap *block_lifetime_map(block *);
#endif

#define BLOCK_DYNMAP_NEW() dynmap_new(block *, NULL, block_hash)
unsigned block_hash(block *);

void block_dump(block *);

void block_lifecheck(block *);

#endif
