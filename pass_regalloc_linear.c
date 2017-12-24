#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include "dynmap.h"
#include "dynarray.h"

#include "val.h"
#include "function.h"
#include "pass_regalloc.h"

#include "lifetime_struct.h"
#include "lifetime.h"
#include "mem.h"
#include "isn.h"
#include "block.h"
#include "target.h"
#include "location.h"

typedef struct interval
{
	val *val;
	unsigned start, end;
}	interval;

struct regalloc_ctx
{
	const struct target *target;
};

static int spaceship(unsigned a, unsigned b)
{
	if(a < b)
		return -1;
	if(a > b)
		return 1;
	return 0;
}

static int sort_interval_end(const void *va, const void *vb)
{
	const interval *const *a = va, *const *b = vb;
	return spaceship((*a)->end, (*b)->end);
}

static unsigned interval_hash(const struct interval *i)
{
	return i->start ^ (i->end << 1);
}

static void expire_old_intervals(
		interval *at_interval,
		dynarray *active,
		dynmap *freeregs,
		bool *reg_is_free)
{
	dynarray active_sorted_end = DYNARRAY_INIT;
	size_t j;

	dynarray_copy(&active_sorted_end, active);
	dynarray_sort(&active_sorted_end, sort_interval_end);

	dynarray_iter(active, j){
		interval *interval = dynarray_ent(&active_sorted_end, j);
		unsigned reg;

		if(interval->end >= at_interval->start){
			break;
		}
		dynarray_splice(active, j, 1);

		reg = dynmap_rm(struct interval *, unsigned, freeregs, interval);
		reg_is_free[reg] = true;

		j--;
	}

	dynarray_reset(&active_sorted_end);
}

static void spill_at_interval(
		interval *at_interval, dynarray *active, dynmap *active_regs)
{
	interval *spill = dynarray_ent(active, dynarray_count(active) - 1);

	if(spill->end > at_interval->end){
		unsigned reg = dynmap_get(interval *, intptr_t,
				active_regs, spill);
		size_t index;

		dynmap_set(interval *, intptr_t,
				active_regs, at_interval, (intptr_t)reg);

		/* TODO: stack location assign to spill */
		index = dynarray_find(active, spill);
		assert(index != -1u);
		dynarray_splice(active, index, 1);
		dynarray_add(active, at_interval);
		dynarray_sort(active, sort_interval_end);
	}else{
		/* TODO: stack location assign to at_interval */
	}
}

static unsigned any_free_reg(bool *reg_is_free, unsigned nregs)
{
	size_t i;
	for(i = 0; i < nregs; i++){
		if(reg_is_free[i]){
			reg_is_free[i] = false;
			return i;
		}
	}
	return -1;
}

static void linear_scan_register_allocation(
		dynarray *intervals /* sorted by start */,
		const unsigned nregs)
{
	dynarray active = DYNARRAY_INIT;
	dynmap *active_regs = dynmap_new(interval *, NULL, interval_hash);
	bool *reg_is_free = xmalloc(nregs);
	size_t i;

	memset(reg_is_free, true, nregs);

	dynarray_iter(intervals, i){
		interval *interval = dynarray_ent(intervals, i);

		expire_old_intervals(interval, &active, active_regs, reg_is_free);

		if(dynarray_count(&active) == nregs){
			fprintf(stderr, "regalloc: spilling %s\n", val_str(interval->val));
			spill_at_interval(interval, &active, active_regs);
		}else{
			struct location *loc;
			unsigned anyreg = any_free_reg(reg_is_free, nregs);

			assert(anyreg != -1u);

			fprintf(stderr, "regalloc: %d into %s\n", anyreg, val_str(interval->val));
			dynmap_set(struct interval *, intptr_t,
					active_regs, interval, (intptr_t)anyreg);

			loc = val_location(interval->val);
			assert(loc);
			loc->where = NAME_IN_REG;
			loc->u.reg = regt_make(anyreg, false);

			dynarray_add(&active, interval);
			dynarray_sort(&active, sort_interval_end);
		}
	}

	dynarray_reset(&active);
	dynmap_free(active_regs);
	free(reg_is_free);
}

static void regalloc_block(block *b, void *vctx)
{
	struct regalloc_ctx *ctx = vctx;
	dynmap *lf_map = block_lifetime_map(b);
	dynarray intervals = DYNARRAY_INIT;
	size_t i;

	for(i = 0;; i++){
		val *v = dynmap_key(val *, lf_map, i);
		struct lifetime *lt = dynmap_value(struct lifetime *, lf_map, i);
		struct interval *interval;
		size_t isn_idx, start = -1, end = -1;
		isn *iter;

		if(!v)
			break;
		if(!lt)
			continue;

		for(iter = block_first_isn(b), isn_idx = 0; iter; iter = isn_next(iter), isn_idx++){
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
		dynarray_add(&intervals, interval);
	}

	linear_scan_register_allocation(&intervals, ctx->target->abi.scratch_regs.count);

	dynarray_iter(&intervals, i)
		free(dynarray_ent(&intervals, i));
	dynarray_reset(&intervals);
}

void pass_regalloc(function *fn, struct unit *unit, const struct target *target)
{
	struct regalloc_ctx ctx;

	ctx.target = target;

	lifetime_fill_func(fn);

	function_onblocks(fn, regalloc_block, &ctx);
}
