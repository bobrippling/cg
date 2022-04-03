#include <stdlib.h>
#include <assert.h>

#include "../dynarray.h"
#include "../dynmap.h"

#include "intervals.h"

#include "interval.h"
#include "../isn.h"
#include "../isn_replace.h"
#include "../isn_struct.h"
#include "../lifetime_struct.h"
#include "../mem.h"
#include "../type.h"
#include "../val_struct.h"
#include "../val_internal.h"
#include "../target.h"
#include "../stack.h"

static int sort_interval_start(const void *va, const void *vb)
{
	const interval *const *a = va, *const *b = vb;
	if((*a)->start < (*b)->start)
		return -1;
	if((*a)->start > (*b)->start)
		return 1;
	return 0;
}

static bool val_is_just_output_on_isn(val *v, isn *i, const struct target *target)
{
	/* Do we need to act as if `v` is a "+" (inout) operand?
	 * Since some backends might use two-operand instructions, we can't do the following:
	 * $a<reg 0> = add $a<reg 0>, $b<reg 1>
	 *
	 * since this may be emitted as
	 * mov %reg1, %reg0
	 * add %reg0, %reg0
	 */
	if(i->type == ISN_OP && target->arch.op_isn_is_destructive)
		return false;

	return isn_defines_val(i, v);
}

void intervals_create(
		dynarray *intervals,
		dynmap *lf_map,
		isn *isn_first,
		struct function *fn,
		const struct target *target)
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
		assert(lt);

		loc = val_location(v);
		assert(loc);

		if(v->live_across_blocks){
			stack_alloc(loc, fn, val_type(v));
			continue;
		}
		if(val_is_mem(v)){
			stack_alloc(loc, fn, type_deref(val_type(v)));
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
		interval->val = val_retain(v);
		interval->loc = loc;
		interval->regspace = 0;
		dynarray_init(&interval->freeregs);

		interval->start_isn = lt->start;

		/* multiply by two - we can then add one to prevent overlaps for output
		 * values, as opposed to input values */
		interval->start = start * 2;
		interval->end = end * 2;

		/* if used as an output on the first or last isn, our life starts/ends post-isn */
		if(val_is_just_output_on_isn(v, lt->start, target))
			interval->start++;
		if(val_is_just_output_on_isn(v, lt->end, target))
			interval->end++;

		dynarray_add(intervals, interval);
	}

	dynarray_sort(intervals, sort_interval_start);
}

void interval_delete(interval *iv)
{
	val_release(iv->val);
	dynarray_reset(&iv->freeregs);
	free(iv);
}

void intervals_delete(dynarray *intervals)
{
	size_t i;
	dynarray_iter(intervals, i){
		interval_delete(dynarray_ent(intervals, i));
	}
	dynarray_reset(intervals);
}
