#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#include "../dynarray.h"
#include "../dynmap.h"

#include "../val.h"
#include "../val_internal.h"
#include "../val_struct.h"
#include "../function.h"
#include "../pass_regalloc.h"

#include "interval.h"
#include "interval_array.h"
#include "../isn.h"
#include "../isn_struct.h"
#include "../lifetime.h"
#include "../lifetime_struct.h"
#include "../mem.h"
#include "../target.h"
#include "../isn_replace.h"
#include "../type.h"
#include "../spill.h"
#include "../stack.h"
#include "free_regs.h"
#include "intervals.h"

/* to get useful debugging info, sort what this emits */
#define REGALLOC_DEBUG 0
#define SPILL_DEBUG 0

struct regalloc_func_ctx
{
	const struct target *target;
	function *function;
	uniq_type_list *utl;
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
			if(toreduce->regspace > 0)
				toreduce->regspace--;

			if(REGALLOC_DEBUG){
				fprintf(stderr, "%s regspace--, because of %s\n",
						val_str_rn(0, toreduce->val),
						val_str_rn(1, from->val));
			}
			break;

		case NAME_IN_REG:
			/* If both are abi regs, and overlap (which they must, if we're here), and
			 * their registers collide, then we just trust whatever code generated
			 * them and don't mark as unused/fail regalloc.
			 *
			 * In the general case, this is avoided by interval allocation ensuring
			 * that input and output values to an instruction don't overlap, meaning
			 * they can share the same register and we won't end up here.
			 *
			 * However in some cases such as x86's idiv being destructive, interval
			 * allocation can't un-overlap the input and output values, meaning
			 * regalloc thinks they must be assigned different registers.
			 * But then x86's idiv requires %eax on input and output, leading to a contradiction.
			 * This is handled explicitly here and cancelled.
			 */
			if(val_is_abi(toreduce->val)
			&& val_is_abi(from->val)
			&& toreduce->loc->where == NAME_IN_REG
			&& toreduce->loc->u.reg == from->loc->u.reg)
			{
				if(REGALLOC_DEBUG){
					fprintf(stderr, "%s freeregs[%#x] would be false because of %s, but both are abi/arch regs\n",
							val_str_rn(0, toreduce->val),
							loc_constraint->u.reg,
							val_str_rn(1, from->val));
				}
				break;
			}

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

static interval *interval_to_spill(struct interval_array *intervals, dynarray *spiltvals)
{
	size_t idx;
	interval_array_iter(intervals, idx){
		interval *iv = interval_array_ent(intervals, idx);

		if((iv->val->flags & SPILL) == 0
		&& dynarray_find(spiltvals, iv->val) == -1)
		{
			dynarray_add(spiltvals, val_retain(iv->val));
			return iv;
		}
	}
	assert(0 && "all spilt?");
}

static void spill_in_interval(
		interval *inside,
		struct interval_array *active_intervals,
		dynarray *intervals,
		function *fn,
		block *block,
		uniq_type_list *utl,
		dynarray *spiltvals)
{
	struct lifetime *spilt_lt;
	interval *tospill = interval_to_spill(active_intervals, spiltvals);
	isn *at = tospill->start_isn;

	if(SPILL_DEBUG){
		fprintf(stderr, "%s: spilling at %s\n", val_str(tospill->val), isn_type_to_str(at->type));
		size_t i;
		dynarray_iter(spiltvals, i){
			val *v = dynarray_ent(spiltvals, i);
			fprintf(stderr, "  spiltvals[%zu] = %s\n", i, val_str(v));
		}
	}

	spill(tospill->val, at, utl, fn, block);

	/* update block lifetime map */
	spilt_lt = dynmap_rm(val *, struct lifetime *, block_lifetime_map(block), tospill->val);
	free(spilt_lt);
}

static bool lsra_space_calc(
		dynarray *intervals,
		dynarray *freeregs,
		function *fn,
		block *block,
		uniq_type_list *utl,
		dynarray *spiltvals)
{
	struct interval_array active_intervals = DYNARRAY_INIT;
	size_t idx;

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
				spill_in_interval(a, &active_intervals, intervals, fn, block, utl, spiltvals);
				goto restart;
			}
		}

		if(!possible(i)){
			spill_in_interval(i, &active_intervals, intervals, fn, block, utl, spiltvals);
			goto restart;
		}

		interval_array_add(&active_intervals, i);
	}

	interval_array_reset(&active_intervals);

	if(REGALLOC_DEBUG){
		dynarray_iter(intervals, idx){
			interval *i = dynarray_ent(intervals, idx);
			size_t regidx;
			const char *sep = "";
			struct location *loc = val_location(i->val);

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
			fprintf(stderr, "} / %zu, constraint %#x\n", regidx, loc->constraint);
		}
	}

	return true;
restart:
	interval_array_reset(&active_intervals);
	return false;
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
			stack_alloc(i->loc, function, val_type(i->val));
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

static void spiltvals_delete(dynarray *spiltvals)
{
	size_t i;
	dynarray_iter(spiltvals, i)
		val_release(dynarray_ent(spiltvals, i));
	dynarray_reset(spiltvals);
}

static void regalloc_block(block *b, void *vctx)
{
	struct regalloc_func_ctx *ctx = vctx;
	dynarray intervals = DYNARRAY_INIT;
	dynarray freeregs = DYNARRAY_INIT;
	dynarray spiltvals = DYNARRAY_INIT;
	dynmap *lifetime_map = block_lifetime_map(b);

	for(;;){
		bool ok;

		intervals_create(&intervals, lifetime_map, block_first_isn(b), ctx->function, ctx->target);
		free_regs_create(&freeregs, ctx->target);

		ok = lsra_space_calc(&intervals, &freeregs, ctx->function, b, ctx->utl, &spiltvals);

		if(ok)
			break;

		free_regs_delete(&freeregs);
		intervals_delete(&intervals);
	}

	lsra_regalloc(&intervals, &freeregs, ctx->function);

	spiltvals_delete(&spiltvals);
	free_regs_delete(&freeregs);
	intervals_delete(&intervals);
}

void pass_regalloc(function *fn, struct unit *unit, const struct target *target)
{
	struct regalloc_func_ctx ctx = { 0 };
	ctx.target = target;
	ctx.function = fn;
	ctx.utl = unit_uniqtypes(unit);

	lifetime_fill_func(fn);

	function_onblocks(fn, regalloc_block, &ctx);
}
