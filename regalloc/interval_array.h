#ifndef INTERVAL_ARRAY_H
#define INTERVAL_ARRAY_H

#include "../dynarray.h"
#include "interval.h"

#define INTERVAL_ARRAY_INIT { DYNARRAY_INIT }

#define interval_array_iter(iar, i) dynarray_iter(&(iar)->ar, i)

typedef struct interval_array {
	dynarray ar;
} interval_array;

size_t interval_array_count(interval_array *);
void interval_array_add(interval_array *, interval *);
void interval_array_rm(interval_array *, interval *);
interval *interval_array_last(interval_array *);
interval *interval_array_ent(interval_array *, size_t);
void interval_array_reset(interval_array *);

#endif
