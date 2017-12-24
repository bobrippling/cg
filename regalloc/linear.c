#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include "../dynarray.h"
#include "../dynmap.h"

#include "../val.h"
#include "../function.h"
#include "../pass_regalloc.h"

#include "interval.h"
#include "interval_array.h"
#include "../isn.h"
#include "../lifetime.h"
#include "../lifetime_struct.h"
#include "../mem.h"
#include "../target.h"

static int sort_interval_start(const void *va, const void *vb)
{
	const interval *const *a = va, *const *b = vb;
	if((*a)->start < (*b)->start)
		return -1;
	if((*a)->start > (*b)->start)
		return 1;
	return 0;
}

static unsigned any_free_reg(dynarray *free_regs)
{
	size_t i;
	for(i = 0; i < dynarray_count(free_regs); i++){
		if(dynarray_ent(free_regs, i)){
			dynarray_ent(free_regs, i) = (void *)(intptr_t)false;
			return i;
		}
	}
	return -1;
}

static void expire_old_intervals(
		interval *i,
		struct interval_array *active_intervals,
		dynarray *free_regs)
{
	const size_t count = interval_array_count(active_intervals);
	size_t idx;

	for(idx = 0; idx < count; idx++){
		interval *j = interval_array_ent(active_intervals, idx);

		if(j->end >= i->start){
			break;
		}

		interval_array_rm(active_intervals, j);

		assert(j->loc->where == NAME_IN_REG);
		dynarray_ent(free_regs, j->loc->u.reg) = (void *)(intptr_t)true;
	}
}

static void spill_at_interval(
		interval *i,
		struct interval_array *active_intervals,
		unsigned *const stack)
{
	interval *spill = interval_array_last(active_intervals);

	if(spill->end > i->end){
		assert(spill->loc->where == NAME_IN_REG);
		*i->loc = *spill->loc;

		spill->loc->where = NAME_SPILT;
		spill->loc->u.off = *stack;
		*stack += 8; /* TODO */

		interval_array_rm(active_intervals, spill);
		interval_array_add(active_intervals, i);

	}else{
		i->loc->where = NAME_SPILT;
		i->loc->u.off = *stack; *stack += 8; /* TODO */
	}
}

static void linear_scan_register_allocation(
		dynarray *intervals /* sorted by start */,
		dynarray *free_regs,
		unsigned *const stack)
{
	const unsigned nregs = dynarray_count(free_regs);
	struct interval_array active_intervals = INTERVAL_ARRAY_INIT;
	size_t idx;

	dynarray_iter(intervals, idx){
		interval *i = dynarray_ent(intervals, idx);

		expire_old_intervals(i, &active_intervals, free_regs);

		if(interval_array_count(&active_intervals) == nregs){
			spill_at_interval(i, &active_intervals, stack);
		}else{
			i->loc->where = NAME_IN_REG;
			i->loc->u.reg = any_free_reg(free_regs);
			assert(i->loc->u.reg != -1u);

			interval_array_add(&active_intervals, i);
		}
	}

	interval_array_reset(&active_intervals);
}

static void create_intervals(dynarray *intervals, dynmap *lf_map, isn *isn_first)
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
		if(!lt)
			continue;
		loc = val_location(v);
		if(!loc)
			continue;

		for(iter = isn_first, isn_idx = 0; iter; iter = isn_next(iter), isn_idx++){
			if(iter == lt->start)
				start = isn_idx;
			if(iter == lt->end)
				end = isn_idx;
		}
		assert(start != -1u && end != -1u);

		interval = xmalloc(sizeof(*interval));
		interval->start = start;
		interval->end = end;
		interval->val = v;
		interval->loc = loc;
		dynarray_add(intervals, interval);
	}

	dynarray_sort(intervals, sort_interval_start);
}

static void delete_intervals(dynarray *intervals)
{
	size_t i;
	dynarray_iter(intervals, i)
		free(dynarray_ent(intervals, i));
	dynarray_reset(intervals);
}

static void get_max_reg(const struct regset *regs, unsigned *const max)
{
	size_t i;
	for(i = 0; i < regs->count; i++)
		if(regs->regs[i] > *max)
			*max = regs->regs[i];
}

static void create_free_regs(dynarray *regs, const struct target *target)
{
	unsigned max = 0;
	size_t i;

	get_max_reg(&target->abi.scratch_regs, &max);
	get_max_reg(&target->abi.callee_saves, &max);
	get_max_reg(&target->abi.arg_regs, &max);
	get_max_reg(&target->abi.ret_regs, &max);

	for(i = 0; i < max; i++)
		dynarray_add(regs, (void *)(intptr_t)false);

	for(i = 0; i < target->abi.scratch_regs.count; i++)
		dynarray_ent(regs, target->abi.scratch_regs.regs[i]) = (void *)(intptr_t)true;
}

static void delete_free_regs(dynarray *regs)
{
	dynarray_reset(regs);
}

static void regalloc_block(block *b, void *vctx)
{
	const struct target *target = vctx;
	dynarray intervals = DYNARRAY_INIT;
	dynarray free_regs = DYNARRAY_INIT;
	unsigned stack = 0;

	create_intervals(&intervals, block_lifetime_map(b), block_first_isn(b));
	create_free_regs(&free_regs, target);

	linear_scan_register_allocation(&intervals, &free_regs, &stack);

	delete_free_regs(&free_regs);
	delete_intervals(&intervals);
}

void pass_regalloc(function *fn, struct unit *unit, const struct target *target)
{
	lifetime_fill_func(fn);

	function_onblocks(fn, regalloc_block, (void *)target);
}
