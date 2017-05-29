#include <stdio.h>
#include <assert.h>

#include "dynmap.h"
#include "mem.h"

#include "function.h"

#include "pass_regalloc.h"

#include "backend.h"
#include "val_struct.h"
#include "val_internal.h"
#include "type.h"
#include "isn.h"
#include "isn_replace.h"
#include "isn_struct.h"
#include "lifetime.h"
#include "regset.h"
#include "regset_marks.h"
#include "target.h"
#include "lifetime.h"
#include "lifetime_struct.h"

#define SHOW_REGALLOC 0
#define SHOW_STACKALLOC 0

#define MAP_GUARDED_VALS 0

struct greedy_ctx
{
	block *blk;
	const struct regset *scratch_regs;
	uniq_type_list *utl;
	dynarray spill_isns;
	dynmap *alloced_vars;
	unsigned *spill_space;
	bool spilt;
};

struct regalloc_ctx
{
	const struct target *target;
	uniq_type_list *utl;
	unsigned spill_space;
};

static unsigned get_spill_space(unsigned *const spill_space, type *ty)
{
	*spill_space += type_size(ty);
	return *spill_space;
}

static void assign_spill(val *spill, unsigned *const spill_space)
{
	struct location *spill_loc = val_location(spill);

	spill_loc->where = NAME_SPILT;
	spill_loc->u.off = get_spill_space(spill_space, type_deref(val_type(spill)));
}

static val *regalloc_spill(val *v, isn *use_isn, struct greedy_ctx *ctx, regt using_reg)
{
	type *const ty = val_type(v);
	val *spill = val_new_localf(
			type_get_ptr(ctx->utl, ty),
			"spill.%d",
			/*something unique:*/(int)v);
	struct lifetime *spill_lt = xmalloc(sizeof *spill_lt);
	struct lifetime *v_lt = dynmap_get(val *, struct lifetime *, block_lifetime_map(ctx->blk), v);
	isn *alloca = isn_alloca(spill);

	assign_spill(spill, ctx->spill_space);

	memcpy(spill_lt, v_lt, sizeof(*spill_lt));
	dynmap_set(val *, struct lifetime *, block_lifetime_map(ctx->blk), spill, spill_lt);

	isn_insert_before(use_isn, alloca);
	isn_replace_uses_with_load_store(v, spill, use_isn, ctx->blk, using_reg);

	ctx->spilt = true;

	return spill;
}

static void mark_in_use_isns(regt reg, struct lifetime *lt)
{
	isn *i;

	for(i = lt->start; i; i = isn_next(i)){
		regset_mark(i->regusemarks, reg, true);

		if(i == lt->end)
			break;
	}
}

static void regalloc_greedy1(val *v, isn *isn, void *vctx)
{
	struct greedy_ctx *ctx = vctx;
	struct lifetime *lt;
	struct location *val_locn;
	val *spill = NULL;

	if(isn->type == ISN_IMPLICIT_USE)
		return;

	if(MAP_GUARDED_VALS){
		if(dynmap_get(val *, long, ctx->alloced_vars, v))
			return;
		dynmap_set(val *, long, ctx->alloced_vars, v, 1L);
	}

	switch(v->kind){
		case LITERAL:
		case GLOBAL:
			/* not something we need to regalloc */
			return;

		case BACKEND_TEMP:
			assert(0 && "BACKEND_TEMP unreachable at this stage");
			return;

		case ALLOCA:
			/* if already spilt, return */
			val_locn = val_location(v);
			if(val_locn->where == NAME_SPILT)
				return;

			assign_spill(v, ctx->spill_space);
			return;

		case ARGUMENT:
		case FROM_ISN:
		case ABI_TEMP:
			/* Not something we need to regalloc,
			 * but we need to account for its register usage.
			 */
			val_locn = val_location(v);
			assert(val_locn);
			break;
	}

	if(type_is_void(val_type(v)))
		return;

	/* if it lives across blocks we use memory */
	if(v->live_across_blocks){
		/* optimisation - ensure the value is in the same register for all blocks
		 * mem2reg or something similar should do this */
		assert(0 && "TODO: grab a free reg for live_across_blocks spill");
		/*regalloc_spill(v, isn, ctx);*/
		return;
	}

	lt = dynmap_get(val *, struct lifetime *, block_lifetime_map(ctx->blk), v);
	assert(lt && "val doesn't have a lifetime");

	if(v->kind == ABI_TEMP){
		assert(val_locn->where == NAME_IN_REG && regt_is_valid(val_locn->u.reg));
		/* should've been marked by the pre-pass */
		assert(regset_is_marked(isn->regusemarks, val_locn->u.reg));
		return;
	}

	if(lt->start != isn){
		return;
	}

	val_retain(v);

	if(!regt_is_valid(val_locn->u.reg)){
		const bool is_fp = type_is_float(val_type(v), 1);
		unsigned i;
		unsigned freecount = 0;
		regt foundreg = regt_make_invalid();

		for(i = 0; i < ctx->scratch_regs->count; i++){
			const regt reg = regt_make(i, is_fp);
			struct isn *isn_iter;
			bool used = false;

			for(isn_iter = lt->start; isn_iter; isn_iter = isn_next(isn_iter)){
				if(regset_is_marked(isn_iter->regusemarks, reg)){
					used = true;
					break;
				}

				if(isn_iter == lt->end){
					break;
				}
			}

			if(!used){
				foundreg = reg;
				freecount++;
			}
		}

		/* should always find at least one register free (for spilling) */
		assert(regt_is_valid(foundreg));

		if(freecount == 1){
			/* no reg available / only one available for spill */
			spill = regalloc_spill(v, isn, ctx, foundreg); /* releases 'v' */

			if(SHOW_REGALLOC){
				fprintf(stderr, "regalloc_spill(%s) => ", val_str(v));
				fprintf(stderr, "%s\n", val_str(spill));
			}

		}else{
			assert(freecount > 1);
			mark_in_use_isns(foundreg, lt);

			if(SHOW_REGALLOC)
				fprintf(stderr, "regalloc(%s) => reg %#x\n", val_str(v), foundreg);

			val_locn->where = NAME_IN_REG;
			val_locn->u.reg = foundreg;
		}
	}

	assert(!v->live_across_blocks);
	assert(spill || (val_locn->where == NAME_IN_REG && regt_is_valid(val_locn->u.reg)));

	val_release(v);
}

static void regalloc_greedy_pre(val *v, isn *isn, void *vctx)
{
	struct greedy_ctx *ctx = vctx;
	struct location *loc;
	struct lifetime *lt;

	loc = val_location(v);
	if(!loc)
		return;
	lt = dynmap_get(val *, struct lifetime *, block_lifetime_map(ctx->blk), v);
	if(!lt)
		return;

	if(loc->where == NAME_IN_REG
	&& regt_is_valid(loc->u.reg))
	{
		mark_in_use_isns(loc->u.reg, lt);
	}
}

static void mark_callee_save_as_used(isn *begin, const struct regset *callee_saves)
{
	struct lifetime all;
	size_t i;

	all.start = begin;
	all.end = NULL;

	for(i = 0; i < callee_saves->count; i++)
		mark_in_use_isns(regset_get(callee_saves, i), &all);
}

static void blk_regalloc_pass(block *blk, void *vctx)
{
	struct regalloc_ctx *ctx = vctx;
	struct greedy_ctx alloc_ctx = { 0 };
	isn *head = block_first_isn(blk);
	isn *isn_iter;

	alloc_ctx.blk = blk;
	alloc_ctx.scratch_regs = &ctx->target->abi.scratch_regs;
	alloc_ctx.spill_space = &ctx->spill_space;
	alloc_ctx.utl = ctx->utl;
	if(MAP_GUARDED_VALS)
		alloc_ctx.alloced_vars = dynmap_new(val *, NULL, val_hash);

	mark_callee_save_as_used(head, &ctx->target->abi.callee_saves);

	/* pre-scan - mark any existing abi regs as used across their lifetime */
	for(isn_iter = head; isn_iter; isn_iter = isn_next(isn_iter)){
		isn_on_live_vals(isn_iter, regalloc_greedy_pre, &alloc_ctx);
	}

#ifdef TWO_PASSES
	bool second = false;
	for(;;){
#endif
		for(isn_iter = head; isn_iter; isn_iter = isn_next(isn_iter)){
			isn_on_live_vals(isn_iter, regalloc_greedy1, &alloc_ctx);
		}

#ifdef TWO_PASSES
		if(!alloc_ctx.spilt){
			fprintf(stderr, "no spills - done\n");
			break;
		}
		if(second){
			fprintf(stderr, "second pass - done\n");
			break;
		}

		fprintf(stderr, "second pass...\n");
		alloc_ctx.spilt = false;
		second = true;
	}
#endif

	if(MAP_GUARDED_VALS)
		dynmap_free(alloc_ctx.alloced_vars);
}

void pass_regalloc(function *fn, struct unit *unit, const struct target *target)
{
	struct regalloc_ctx ctx = { 0 };
	block *entry = function_entry_block(fn, false);
	dynmap *alloc_markers;

	if(!entry)
		return;

	alloc_markers = BLOCK_DYNMAP_NEW();

	ctx.target = target;
	ctx.utl = unit_uniqtypes(unit);

	lifetime_fill_func(fn);

	blocks_traverse(entry, blk_regalloc_pass, &ctx, alloc_markers);

	/* FIXME: deal with ctx.spill_space */

	dynmap_free(alloc_markers);
}
