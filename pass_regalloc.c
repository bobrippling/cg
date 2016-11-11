#include <stdio.h>
#include <assert.h>

#include "dynmap.h"

#include "function.h"

#include "pass_regalloc.h"

#include "backend.h"
#include "val_struct.h"
#include "type.h"
#include "isn.h"
#include "isn_replace.h"
#include "isn_struct.h"
#include "lifetime.h"
#include "regset.h"
#include "target.h"
#include "lifetime.h"
#include "lifetime_struct.h"

#define SHOW_REGALLOC 1
#define SHOW_STACKALLOC 1

struct greedy_ctx
{
	block *blk;
	const struct regset *scratch_regs;
	uniq_type_list *utl;
	regset_marks in_use;
	unsigned *spill_space;
	unsigned isn_num;
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
			"spill.%u",
			/*something unique:*/ctx->isn_num);
	isn *alloca = isn_alloca(spill);

	isn_insert_before(use_isn, alloca);

	isn_replace_uses_with_load_store(v, spill, use_isn, ctx->blk);

	return spill;
}

static void regalloc_mirror(val *dest, val *src)
{
	fprintf(stderr, "TODO: regalloc_mirror()\n");
#if 0
	val_mirror(dest, src);
#endif
}

static void regalloc_greedy1(val *v, isn *isn, void *vctx)
{
	bool needs_regalloc;
	struct greedy_ctx *ctx = vctx;
	struct lifetime *lt;
	struct location *val_locn;
	val *src, *dest;

	switch(v->kind){
		case LITERAL:
		case GLOBAL:
			/* not something we need to regalloc */
			return;

		case ABI_TEMP:
			return;

		case ALLOCA:
			/* stack automatically */
			return;

		case ARGUMENT:
		case FROM_ISN:
			val_locn = val_location(v);
			assert(val_locn);
			break;

		case BACKEND_TEMP:
			return;
	}

	if(type_is_void(val_type(v)))
		return;

	/* if the instruction is a no-op (e.g. ptrcast, ptr2int/int2ptr where the sizes match),
	 * then we reuse the source register/spill */
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
	if(!lt){
		fprintf(stderr, "%s doesn't have a lifetime\n", val_str(v));
		return;
	}
	assert(lt);

	needs_regalloc = !regt_is_valid(val_locn->u.reg) && lt->start == ctx->isn_num;

	if(needs_regalloc){
		const bool is_fp = type_is_float(val_type(v), 1);
		unsigned i;
		regt reg = regt_make_invalid();

		for(i = 0; i < ctx->scratch_regs->count; i++){
			reg = regt_make(i, is_fp);
			if(!regset_is_marked(ctx->in_use, reg))
				break;
		}

		if(i == ctx->scratch_regs->count){
			/* no reg available */
			val *spill = regalloc_spill(v, isn, ctx);

			if(SHOW_REGALLOC){
				fprintf(stderr, "regalloc_spill(%s) => ", val_str(v));
				fprintf(stderr, "%s\n", val_str(spill));
			}

		}else{
			assert(regt_is_valid(reg));

			regset_mark(&ctx->in_use, reg, true);

			if(SHOW_REGALLOC)
				fprintf(stderr, "regalloc(%s) => reg %d\n", val_str(v), i);

			val_locn->where = NAME_IN_REG;
			val_locn->u.reg = reg;
		}
	}

	assert(!v->live_across_blocks);
	assert(val_locn->where == NAME_IN_REG);

	assert(regt_is_valid(val_locn->u.reg) == regset_is_marked(ctx->in_use, val_locn->u.reg));

	if(ctx->isn_num >= lt->end && regt_is_valid(val_locn->u.reg)){
		regset_mark(&ctx->in_use, val_locn->u.reg, false);
	}
}

static void mark_callee_save_as_used(
		regset_marks *marks, const struct regset *callee_saves)
{
	size_t i;
	for(i = 0; i < callee_saves->count; i++)
		regset_mark(marks, regset_get(callee_saves, i), true);
}

static void blk_regalloc_pass(block *blk, void *vctx)
{
	struct regalloc_ctx *ctx = vctx;
	struct greedy_ctx alloc_ctx = { 0 };
	isn *head = block_first_isn(blk);
	isn *isn_iter;

	alloc_ctx.blk = blk;
	alloc_ctx.scratch_regs = &ctx->target->abi.scratch_regs;
	alloc_ctx.in_use = 0x0;
	alloc_ctx.spill_space = &ctx->spill_space;
	alloc_ctx.utl = ctx->utl;

	mark_callee_save_as_used(
			&alloc_ctx.in_use,
			&ctx->target->abi.callee_saves);

	for(isn_iter = head; isn_iter; isn_iter = isn_next(isn_iter), alloc_ctx.isn_num++)
		isn_on_live_vals(isn_iter, regalloc_greedy1, &alloc_ctx);
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
