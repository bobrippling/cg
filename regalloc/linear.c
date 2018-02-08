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
#include "free_regs.h"
#include "intervals.h"
#include "stack.h"

#define REGALLOC_DEBUG 1

struct regalloc_func_ctx
{
	const struct target *target;
	function *function;
};

attr_nonnull((1, 2))
static void expire_old_intervals(
		interval *i,
		struct interval_array *active_intervals,
		dynarray *freeregs)
{
	size_t count = interval_array_count(active_intervals);
	size_t idx;

	for(idx = 0; idx < count; idx++){
		interval *j = interval_array_ent(active_intervals, idx);

		if(j->end >= i->start){
			break;
		}
		/* i->start > j->end: j has expired*/

		interval_array_rm(active_intervals, j);
		idx--;
		count--;

		if(freeregs){
			assert(j->loc->where == NAME_IN_REG);
			dynarray_ent(freeregs, j->loc->u.reg) = (void *)(intptr_t)true;
		}
	}
}

static bool possible(interval *interval)
{
	enum location_constraint req = interval->loc->constraint;

	if(req & CONSTRAINT_MEM)
		return true;

	if(req & CONSTRAINT_REG){
		switch(interval->loc->where){
			case NAME_IN_REG_ANY:
				return interval->regspace > 0;
			case NAME_IN_REG:
			{
				/* constraint is reg-specific */
				regt reg = interval->loc->u.reg;
				return dynarray_ent(&interval->freeregs, reg);
			}
			case NAME_NOWHERE:
			case NAME_SPILT:
				return true;
		}
	}

	if(req == CONSTRAINT_NONE)
		return true;

	assert(0 && "unreachable");
}

static void reduce_interval_from_interval(interval *toreduce, interval *from)
{
	struct location *loc_constraint = from->loc;

	if((loc_constraint->constraint & CONSTRAINT_REG) == 0){
		if(REGALLOC_DEBUG){
			fprintf(stderr, "%s nothing to reduce (%s not constrained to reg)\n",
					val_str_rn(0, toreduce->val),
					val_str_rn(1, from->val));
		}
		return;
	}

	switch(loc_constraint->where){
		case NAME_NOWHERE:
		case NAME_SPILT:
			if(REGALLOC_DEBUG){
				fprintf(stderr, "%s nothing to reduce (%s where %#x)\n",
						val_str_rn(0, toreduce->val),
						val_str_rn(1, from->val),
						loc_constraint->where);
			}
			break;

		case NAME_IN_REG_ANY:
			toreduce->regspace--;

			if(REGALLOC_DEBUG){
				fprintf(stderr, "%s regspace--, because of %s\n",
						val_str_rn(0, toreduce->val),
						val_str_rn(1, from->val));
			}
			break;

		case NAME_IN_REG:
			dynarray_ent(&toreduce->freeregs, loc_constraint->u.reg) = (void *)(intptr_t)false;

			if(REGALLOC_DEBUG){
				fprintf(stderr, "%s freeregs[%#x]=false, because of %s\n",
						val_str_rn(0, toreduce->val),
						loc_constraint->u.reg,
						val_str_rn(1, from->val));
			}
			break;
	}
}

static void lsra_space_calc(dynarray *intervals, dynarray *freeregs)
{
	struct interval_array active_intervals = INTERVAL_ARRAY_INIT;
	size_t idx;
	bool impossible = false;

	dynarray_iter(intervals, idx){
		interval *i = dynarray_ent(intervals, idx);
		size_t jdx;

		i->regspace = dynarray_count(freeregs);
		dynarray_copy(&i->freeregs, freeregs);

		expire_old_intervals(i, &active_intervals, NULL);

		interval_array_iter(&active_intervals, jdx){
			interval *a = interval_array_ent(&active_intervals, jdx);

			if(a == i)
				continue;

			reduce_interval_from_interval(a, i);
			reduce_interval_from_interval(i, a);

			if(!possible(a)){
				fprintf(stderr, "can't constrain: %s has nowhere to go\n", val_str(a->val));
				impossible = true;
			}
		}

		if(!possible(i)){
			fprintf(stderr, "can't constrain: %s has nowhere to go\n", val_str(i->val));
			impossible = true;
		}

		interval_array_add(&active_intervals, i);
	}

	interval_array_reset(&active_intervals);

	if(REGALLOC_DEBUG){
		dynarray_iter(intervals, idx){
			interval *i = dynarray_ent(intervals, idx);
			size_t regidx;
			const char *sep = "";

			fprintf(stderr, "%s: live={%u-%u} regspace=%u freeregs={",
					val_str(i->val),
					i->start,
					i->end,
					i->regspace);

			dynarray_iter(&i->freeregs, regidx){
				if(dynarray_ent(&i->freeregs, regidx)){
					fprintf(stderr, "%s%zu", sep, regidx);
					sep = ", ";
				}
			}
			fprintf(stderr, "} / %zu\n", regidx);
		}
	}

	assert(!impossible && "impossible to satisfy regalloc constraints");
}

static void lsra_regalloc(dynarray *intervals, dynarray *freeregs, function *function)
{
	struct interval_array active_intervals = INTERVAL_ARRAY_INIT;
	size_t idx;

	dynarray_iter(intervals, idx){
		interval *i = dynarray_ent(intervals, idx);
		dynarray merged_regs = DYNARRAY_INIT;

		expire_old_intervals(i, &active_intervals, freeregs);

		if(location_fully_allocated(i->loc))
			continue;

		free_regs_merge(&merged_regs, &i->freeregs, freeregs);

		if(i->regspace == 0 || free_regs_available(&merged_regs) == 0){
			lsra_stackalloc(i->loc, function, val_type(i->val));
		} else {
			i->loc->where = NAME_IN_REG;

			i->loc->u.reg = free_regs_any(&merged_regs);
			assert(i->loc->u.reg != -1);

			dynarray_ent(freeregs, i->loc->u.reg) = (void *)(intptr_t)false;
			interval_array_add(&active_intervals, i);
		}

		dynarray_reset(&merged_regs);
	}

	interval_array_reset(&active_intervals);
}

static void lsra_constrained(dynarray *intervals, const struct target *target, function *function)
{
	dynarray freeregs = DYNARRAY_INIT;

	free_regs_create(&freeregs, target);

	lsra_space_calc(intervals, &freeregs);
	lsra_regalloc(intervals, &freeregs, function);

	free_regs_delete(&freeregs);
}

static void regalloc_block(block *b, void *vctx)
{
	struct regalloc_func_ctx *ctx = vctx;
	dynarray intervals = DYNARRAY_INIT;

	intervals_create(&intervals, block_lifetime_map(b), block_first_isn(b), ctx->function);

	lsra_constrained(&intervals, ctx->target, ctx->function);

	intervals_delete(&intervals);
}

void pass_regalloc(function *fn, struct unit *unit, const struct target *target)
{
	struct regalloc_func_ctx ctx = { 0 };
	ctx.target = target;
	ctx.function = fn;

	(void)unit;

	lifetime_fill_func(fn);

	function_onblocks(fn, regalloc_block, &ctx);
}
