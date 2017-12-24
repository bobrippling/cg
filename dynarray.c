#include <stdlib.h>
#include <string.h>

#include "dynarray.h"
#include "mem.h"

void dynarray_add(dynarray *d, void *ent)
{
	d->n++;

	if(d->n >= d->capacity){
		if(d->capacity == 0){
			d->capacity = 16;
		}else{
			d->capacity *= 1.5;
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
	size_t i;

	dynarray_reset(dest);

	memcpy(dest, src, sizeof *dest);

	if(dynarray_count(src)){
		dest->entries = xmalloc(dynarray_count(src) * sizeof *dest->entries);

		dynarray_iter(src, i){
			dynarray_ent(dest, i) = dynarray_ent(src, i);
		}
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
	qsort(
			&dynarray_ent(ar, 0),
			dynarray_count(ar),
			sizeof(dynarray_ent(ar, 0)),
			compar);
}
