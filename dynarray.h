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

#define dynarray_is_empty(d) ((d)->n == 0)
#define dynarray_ent(d, i) ((d)->entries[i])
#define dynarray_iter(d, i) for(i = 0; i < (d)->n; i++)

#endif
