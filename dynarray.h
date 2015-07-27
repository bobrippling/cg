#ifndef DYNARRAY_H
#define DYNARRAY_H

#include <stdbool.h>
#include <stddef.h>

typedef struct dynarray
{
	void **entries;
	size_t n;
	size_t capacity;
} dynarray;

#define DYNARRAY_INIT { 0 }

void dynarray_add(dynarray *, void *);
void dynarray_reset(dynarray *);
void dynarray_move(dynarray *dest, dynarray *src);
bool dynarray_refeq(dynarray *, dynarray *);

#define dynarray_init(d) memset((d), 0, sizeof(*(d)))
#define dynarray_is_empty(d) ((d)->n == 0)
#define dynarray_ent(d, i) ((d)->entries[i])
#define dynarray_iter(d, i) for(i = 0; i < (d)->n; i++)
#define dynarray_count(d) ((d)->n)

#endif
