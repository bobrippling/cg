#include <stdlib.h>
#include <string.h>

#include "dynarray.h"
#include "mem.h"

#ifdef NDEBUG
#  define DYNARRAY_ALLOC_EXACT 0
#else
#  define DYNARRAY_ALLOC_EXACT 1
#endif

void dynarray_add(dynarray *d, void *ent)
{
	d->n++;

	if(d->n >= d->capacity){
		if(DYNARRAY_ALLOC_EXACT){
			d->capacity++;
		}else{
			if(d->capacity == 0){
				d->capacity = 16;
			}else{
				d->capacity *= 1.5;
			}
		}

		d->entries = xrealloc(d->entries, d->capacity * sizeof *d->entries);
	}

	d->entries[d->n - 1] = ent;
}

void dynarray_reset(dynarray *d)
{
	free(d->entries);
	memset(d, 0, sizeof *d);
}

void dynarray_move(dynarray *dest, dynarray *src)
{
	dynarray_reset(dest);

	memcpy(dest, src, sizeof *dest);

	src->entries = NULL;
	dynarray_reset(src);
}

void dynarray_copy(dynarray *dest, dynarray *src)
{
	dynarray_reset(dest);

	if(dynarray_count(src)){
		dest->capacity = dynarray_count(src);
		dest->n = dest->capacity;
		dest->entries = xmalloc(dest->capacity * sizeof *dest->entries);

		memcpy(dest->entries, src->entries, dest->n * sizeof *dest->entries);
	}
}

bool dynarray_refeq(dynarray *a, dynarray *b)
{
	size_t i;

	if(dynarray_count(a) != dynarray_count(b))
		return false;

	dynarray_iter(a, i){
		if(dynarray_ent(a, i) != dynarray_ent(b, i))
			return false;
	}

	return true;
}

void dynarray_foreach(dynarray *d, void fn(void *))
{
	size_t i;
	dynarray_iter(d, i){
		fn(dynarray_ent(d, i));
	}
}

void dynarray_splice(dynarray *d, size_t from, size_t count)
{
	size_t end = from + count;
	size_t movecount = dynarray_count(d) - end;

	memmove(
			&d->entries[from],
			&d->entries[from + count],
			sizeof(void *) * movecount);

	dynarray_count(d) -= count;
}

size_t dynarray_find(dynarray *d, void *p)
{
	size_t i;
	dynarray_iter(d, i)
		if(dynarray_ent(d, i) == p)
			return i;
	return -1;
}

void dynarray_sort(dynarray *ar, int compar(const void *, const void *))
{
	if(dynarray_count(ar) == 0)
		return; /* ub to pass null to qsort() */
	qsort(
			&dynarray_ent(ar, 0),
			dynarray_count(ar),
			sizeof(dynarray_ent(ar, 0)),
			compar);
}

void dynarray_fill(dynarray *ar, void *ent, size_t n)
{
	while(n --> 0)
		dynarray_add(ar, ent);
}
