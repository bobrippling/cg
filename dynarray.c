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
