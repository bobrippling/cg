#ifndef DYNARRAY_H
#define DYNARRAY_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

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
void dynarray_copy(dynarray *dest, dynarray *src);
bool dynarray_refeq(dynarray *, dynarray *);
void dynarray_foreach(dynarray *, void (void *));
void dynarray_splice(dynarray *, size_t from, size_t count);
size_t dynarray_find(dynarray *, void *);
void dynarray_sort(dynarray *, int (const void *, const void *));

#define dynarray_init(d) memset((d), 0, sizeof(*(d)))
#define dynarray_is_empty(d) ((d)->n == 0)
#define dynarray_ent(d, i) ((d)->entries[i])
#define dynarray_iter(d, i) for(i = 0; i < (d)->n; i++)
#define dynarray_count(d) ((d)->n)

#endif
