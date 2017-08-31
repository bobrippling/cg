#ifndef BLOCK_H
#define BLOCK_H

#include <stdbool.h>
#include "macros.h"

struct val;
struct isn;
struct dynmap;

typedef struct block block;

enum block_type
{
	BLK_UNKNOWN,
	BLK_ENTRY,
	BLK_EXIT,
	BLK_BRANCH,
	BLK_JMP
};

bool block_unknown_ending(block *);

void block_free(block *);

int block_tenative(block *);

void blocks_traverse(block *, void (block *, void *), void *);

struct isn *block_first_isn(block *);
void block_add_isn(block *, struct isn *);

void block_set_type(block *blk, enum block_type type);
void block_set_branch(
		block *current, struct val *cond, block *btrue, block *bfalse);
void block_set_jmp(block *current, block *target);

#ifdef DYNMAP_H
dynmap *block_lifetime_map(block *);
#endif

#define BLOCK_DYNMAP_NEW() dynmap_new(block *, NULL, block_hash)
unsigned block_hash(block *);

void block_dump(block *);

void block_lifecheck(block *);

#endif
