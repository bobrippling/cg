#include <stdlib.h>
#include <assert.h>

#include "../dynarray.h"
#include "../dynmap.h"

#include "intervals.h"

#include "interval.h"
#include "../isn.h"
#include "../lifetime_struct.h"
#include "../mem.h"
#include "../type.h"
#include "../val_struct.h"
#include "stack.h"

static int sort_interval_start(const void *va, const void *vb)
{
	const interval *const *a = va, *const *b = vb;
	if((*a)->start < (*b)->start)
		return -1;
	if((*a)->start > (*b)->start)
		return 1;
	return 0;
}


void intervals_create(
		dynarray *intervals,
		dynmap *lf_map,
		isn *isn_first,
		struct function *fn)
{
	size_t i;
	for(i = 0;; i++){
		val *v = dynmap_key(val *, lf_map, i);
		struct lifetime *lt = dynmap_value(struct lifetime *, lf_map, i);
		struct interval *interval;
		size_t isn_idx, start = -1, end = -1;
		struct location *loc;
		isn *iter;

		if(!v)
			break;
		if(!lt){
			fprintf(stderr, "no lt: %s\n", val_str(v));
			continue;
		}
		loc = val_location(v);
		if(!loc){
			fprintf(stderr, "no loc: %s\n", val_str(v));
			continue;
		}
		if(v->live_across_blocks){
			lsra_stackalloc(loc, fn, val_type(v));
			continue;
		}
		if(val_is_mem(v)){
			lsra_stackalloc(loc, fn, type_deref(val_type(v)));
			continue;
		}

		for(iter = isn_first, isn_idx = 0; iter; iter = isn_next(iter), isn_idx++){
			if(iter == lt->start)
				start = isn_idx;
			if(iter == lt->end)
				end = isn_idx;
		}
		assert(start != -1 && end != -1);

		interval = xmalloc(sizeof(*interval));
		interval->start = start;
		interval->end = end;
		interval->val = v;
		interval->loc = loc;
		interval->regspace = 0;
		dynarray_init(&interval->freeregs);

		dynarray_add(intervals, interval);
	}

	dynarray_sort(intervals, sort_interval_start);
}

void intervals_delete(dynarray *intervals)
{
	size_t i;
	dynarray_iter(intervals, i){
		interval *iv = dynarray_ent(intervals, i);

		dynarray_reset(&iv->freeregs);
		free(iv);
	}
	dynarray_reset(intervals);
}
