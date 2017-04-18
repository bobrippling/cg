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

struct greedy_ctx
{
	block *blk;
	const struct regset *scratch_regs;
	uniq_type_list *utl;
	dynarray spill_isns;
	unsigned *spill_space;
};

struct regalloc_ctx
{
	const struct target *target;
	uniq_type_list *utl;
	unsigned spill_space;
};

static val *regalloc_spill(val *v, isn *use_isn, struct greedy_ctx *ctx)
{
	type *const ty = val_type(v);
	val *spill = val_new_localf(
			type_get_ptr(ctx->utl, ty),
			"spill.%d",
			/*something unique:*/(int)v);
	struct lifetime *spill_lt = xmalloc(sizeof *spill_lt);
	struct lifetime *v_lt = dynmap_get(val *, struct lifetime *, block_lifetime_map(ctx->blk), v);
	isn *alloca = isn_alloca(spill);

	memcpy(spill_lt, v_lt, sizeof(*spill_lt));
	dynmap_set(val *, struct lifetime *, block_lifetime_map(ctx->blk), spill, spill_lt);

	isn_insert_before(use_isn, alloca);
	isn_replace_uses_with_load_store(v, spill, use_isn, ctx->blk, using_reg);


	return spill;
}

static void regalloc_mirror(val *dest, val *src)
{
	fprintf(stderr, "TODO: regalloc_mirror()\n");
#if 0
	val_mirror(dest, src);
#endif
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
	bool needs_regalloc;
	struct greedy_ctx *ctx = vctx;
	struct lifetime *lt;
	struct location *val_locn;
	val *src, *dest;
	val *spill = NULL;
	regset_marks this_isn_marks;
	this_isn_marks = isn->regusemarks;

	if(isn->type == ISN_IMPLICIT_USE)
		return;

	switch(v->kind){
		case LITERAL:
		case GLOBAL:
			/* not something we need to regalloc */
			return;

		case ALLOCA:
			/* stack automatically */
			return;

		case ARGUMENT:
		case FROM_ISN:
			val_locn = val_location(v);
			assert(val_locn);
			break;

		case ABI_TEMP:
			/* Not something we need to regalloc,
			 * but we need to account for its register usage.
			 *
			 * A second regalloc won't occur because of the
			 * regt_is_valid() check lower down */
			val_locn = val_location(v);
			assert(val_locn);
			assert(regt_is_valid(val_locn->u.reg));
			break;

		case BACKEND_TEMP:
			return;
	}

	if(type_is_void(val_type(v)))
		return;

	/* if the instruction is a no-op (e.g. ptrcast, ptr2int/int2ptr where the sizes match),
	 * then we reuse the source register/spill */
	/* FIXME: don't think this is safe because of lifetime checks for the earlier val */
	if(isn_is_noop(isn, &src, &dest)){
		if(v == src){
			/* if we're the source register, we need allocation */
		}else{
			if(SHOW_REGALLOC){
				fprintf(stderr, "regalloc(%s) => reuse of source register\n", val_str(v));
			}

			assert(v == dest);
			regalloc_mirror(dest, src);
			return;
		}
	}

	/* if it lives across blocks we use memory */
	if(v->live_across_blocks){
		/* optimisation - ensure the value is in the same register for all blocks
		 * mem2reg or something similar should do this */
		regalloc_spill(v, isn, ctx);
		return;
	}

	/* if already spilt, return */
	if(val_locn->where == NAME_SPILT)
		return;

	lt = dynmap_get(val *, struct lifetime *, block_lifetime_map(ctx->blk), v);
	assert(lt && "val doesn't have a lifetime");

	needs_regalloc = !regt_is_valid(val_locn->u.reg) && lt->start == isn;

	val_retain(v);

	if(needs_regalloc){
		const bool is_fp = type_is_float(val_type(v), 1);
		unsigned i;
		regt reg = regt_make_invalid();

		for(i = 0; i < ctx->scratch_regs->count; i++){
			reg = regt_make(i, is_fp);
			if(!regset_is_marked(this_isn_marks, reg))
				break;
		}

		if(i == ctx->scratch_regs->count){
			/* no reg available */
			spill = regalloc_spill(v, isn, ctx); /* releases 'v' */

			if(SHOW_REGALLOC){
				fprintf(stderr, "regalloc_spill(%s) => ", val_str(v));
				fprintf(stderr, "%s\n", val_str(spill));
			}

		}else{
			assert(regt_is_valid(reg));

			mark_in_use_isns(reg, lt);

			if(SHOW_REGALLOC)
				fprintf(stderr, "regalloc(%s) => reg %d\n", val_str(v), i);

			val_locn->where = NAME_IN_REG;
			val_locn->u.reg = reg;
		}
	}else if(v->kind == ABI_TEMP){
		assert(regt_is_valid(val_locn->u.reg));
		mark_in_use_isns(val_locn->u.reg, lt);
	}

	assert(!v->live_across_blocks);
	assert(val_locn->where == NAME_IN_REG);

	if(!spill){
		/* HACK: we should populate a spill val's regset-marks when spilling */
		int good = (!regt_is_valid(val_locn->u.reg) || regset_is_marked(this_isn_marks, val_locn->u.reg));
		/*assert(good);*/
		if(!good){
			fprintf(stderr, "!good\n");
		}
	}

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

	mark_callee_save_as_used(head, &ctx->target->abi.callee_saves);

	/* pre-scan - mark any existing abi regs as used across their lifetime */
	for(isn_iter = head; isn_iter; isn_iter = isn_next(isn_iter)){
		isn_on_live_vals(isn_iter, regalloc_greedy_pre, &alloc_ctx);
	}

	for(isn_iter = head; isn_iter; isn_iter = isn_next(isn_iter)){
		isn_on_live_vals(isn_iter, regalloc_greedy1, &alloc_ctx);
	}

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

	/* not sure what to do with this for now...
	 *alloca = ctx.spill_space; */

	dynmap_free(alloc_markers);
}
