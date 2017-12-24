#include <stdint.h>
#include <assert.h>

#include "interval_array.h"

static int sort_interval_end(const void *va, const void *vb)
{
	const interval *const *a = va, *const *b = vb;
	if((*a)->end < (*b)->end)
		return -1;
	if((*a)->end > (*b)->end)
		return 1;
	return 0;
}

size_t interval_array_count(interval_array *a)
{
	return dynarray_count(&a->ar);
}

void interval_array_add(interval_array *a, interval *i)
{
	dynarray_add(&a->ar, i);
	dynarray_sort(&a->ar, sort_interval_end);
}

void interval_array_rm(interval_array *a, interval *i)
{
	size_t idx = dynarray_find(&a->ar, i);
	assert(idx != -1u);
	dynarray_splice(&a->ar, idx, 1);
}

interval *interval_array_last(interval_array *a)
{
	return interval_array_ent(a, interval_array_count(a) - 1);
}

interval *interval_array_ent(interval_array *a, size_t i)
{
	return dynarray_ent(&a->ar, i);
}

void interval_array_reset(interval_array *a)
{
	dynarray_reset(&a->ar);
}
