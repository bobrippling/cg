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
#define REGALLOC_DEBUG 1
#define SPILL_DEBUG 1

struct regalloc_func_ctx
{
	const struct target *target;
	function *function;
	uniq_type_list *utl;
	block *block;
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

static void lsra_space_calc(
		dynarray *intervals,
		dynarray *freeregs,
		function *fn,
		block *block,
		uniq_type_list *utl)
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

			/* It may be impossible to have `a` in its desired location here.
			 * Doesn't matter - our allocation will notice and correctly perform
			 * spills later on
			 */
		}

		/* same here, for `i` */

		interval_array_add(&active_intervals, i);
	}

	interval_array_reset(&active_intervals);

	if(REGALLOC_DEBUG){
		dynarray_iter(intervals, idx){
			interval *i = dynarray_ent(intervals, idx);
			size_t regidx;
			const char *sep = "";
			struct location *loc = val_location(i->val);

			fprintf(stderr, "%s: live={%u%s-%u%s} regspace=%u freeregs={",
					val_str(i->val),
					i->start / 2,
					i->start % 2 ? "+" : "",
					i->end / 2,
					i->end % 2 ? "+" : "",
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
}

static void lsra_regalloc(dynarray *intervals, dynarray *freeregs, struct regalloc_func_ctx *ctx)
{
	struct interval_array active_intervals = INTERVAL_ARRAY_INIT;
	size_t idx = 0;
	interval *next_interval = dynarray_ent(intervals, idx);
	isn *isn_iter;

	for(isn_iter = block_first_isn(ctx->block); isn_iter;){
		if(next_interval && isn_iter == next_interval->start_isn){
			dynarray merged_regs;
			interval *i = next_interval;

			idx++;
			if(idx == dynarray_count(intervals)){
				next_interval = NULL;
			}else{
				assert(idx < dynarray_count(intervals));
				next_interval = dynarray_ent(intervals, idx);
			}

			expire_old_intervals(i, &active_intervals, freeregs);

			if(location_fully_allocated(i->loc))
				continue;

			free_regs_merge(&merged_regs, &i->freeregs, freeregs);

			if(i->regspace == 0 || free_regs_available(&merged_regs) == 0){
				if(i->loc->constraint & CONSTRAINT_REG || i->loc->where == NAME_IN_REG_ANY){
					/* This is the tough part - the instruction requires a register, but we have none.
					 * We pick an unrelated (i.e. unused in in the 'current' isn) register, spill it,
					 * and restore when it's next used.
					 *
					 * FIXME: this means a value is live in potentially different regs. Can no longer set
					 * a register on a value's .u.local.loc.u.reg */
					spill(i->val, i->start_isn, ctx->utl, ctx->function, ctx->block);
				}else{
					stack_alloc(i->loc, ctx->function, val_type(i->val));
				}
			} else {
				i->loc->where = NAME_IN_REG;

				i->loc->u.reg = free_regs_any(&merged_regs);
				assert(i->loc->u.reg != -1);

				dynarray_ent(freeregs, i->loc->u.reg) = (void *)(intptr_t)false;
				interval_array_add(&active_intervals, i);
			}

			dynarray_reset(&merged_regs);
		}else{
			isn_iter = isn_next(isn_iter);
		}
	}
	assert(next_interval == NULL);

	interval_array_reset(&active_intervals);
}

static void regalloc_block(block *b, void *vctx)
{
	struct regalloc_func_ctx *ctx = vctx;
	dynarray intervals = DYNARRAY_INIT;
	dynarray freeregs = DYNARRAY_INIT;
	dynmap *lifetime_map = block_lifetime_map(b);

	ctx->block = b;

	intervals_create(&intervals, lifetime_map, block_first_isn(b), ctx->function, ctx->target);
	free_regs_create(&freeregs, ctx->target);

	lsra_space_calc(&intervals, &freeregs, ctx->function, b, ctx->utl);

	lsra_regalloc(&intervals, &freeregs, ctx);

	free_regs_delete(&freeregs);
	intervals_delete(&intervals);

	ctx->block = NULL;
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
