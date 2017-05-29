#include <assert.h>
#include <stdio.h>

#include "dynmap.h"
#include "mem.h"

#include "function.h"
#include "unit.h"
#include "val.h"
#include "val_struct.h"
#include "lifetime.h"
#include "lifetime_struct.h"
#include "isn.h"
#include "isn_replace.h"
#include "isn_struct.h"
#include "target.h"
#include "type.h"

#include "pass_spill.h"
#include "pass_regalloc.h"

struct spill_ctx
{
	block *blk;
	struct uniq_type_list *utl;
	unsigned free_count;
	unsigned regcount;
	unsigned spill_space;
};

static unsigned get_spill_space(unsigned *const spill_space, type *ty)
{
	*spill_space += type_size(ty);
	return *spill_space;
}

static void spill_assign(val *spill, unsigned *const spill_space)
{
	struct location *spill_loc = val_location(spill);

	spill_loc->where = NAME_SPILT;
	spill_loc->u.off = get_spill_space(spill_space, type_deref(val_type(spill)));
}

static void spill(val *v, isn *use_isn, struct spill_ctx *ctx)
{
	type *const ty = val_type(v);
	val *spill = val_new_localf(
			type_get_ptr(ctx->utl, ty),
			"spill.%d",
			/*something unique:*/(int)v);
	struct lifetime *spill_lt = xmalloc(sizeof *spill_lt);
	struct lifetime *v_lt = dynmap_get(val *, struct lifetime *, block_lifetime_map(ctx->blk), v);
	isn *alloca = isn_alloca(spill);

	spill_assign(spill, &ctx->spill_space);

	memcpy(spill_lt, v_lt, sizeof(*spill_lt));
	dynmap_set(val *, struct lifetime *, block_lifetime_map(ctx->blk), spill, spill_lt);

	isn_insert_before(use_isn, alloca);

	/* no reg overlap - we just setup the values, regalloc can deal with the rest */
	isn_replace_uses_with_load_store(v, spill, use_isn, ctx->blk);
}

static void isn_spill(val *v, isn *isn, void *vctx)
{
	struct spill_ctx *ctx = vctx;
	const struct lifetime *lt;

	if(v->kind == ALLOCA && isn->type == ISN_ALLOCA){
		spill_assign(v, &ctx->spill_space);
		return;
	}

	if(!regalloc_applies_to(v))
		return;

	if(v->live_across_blocks){
		/* NOTE: we assume it's spilt */
		return;
	}

	lt = dynmap_get(
			val *,
			struct lifetime *,
			block_lifetime_map(ctx->blk), v);

	if(!lt){
		return;
	}

	if(lt->start == isn){
		ctx->free_count++;

		if(ctx->free_count >= ctx->regcount - 2){
			spill(v, isn, ctx);
		}

	}else if(lt->end == isn){
		ctx->free_count--;
	}
}

static void blk_spill(block *blk, void *vctx)
{
	struct spill_ctx *ctx = vctx;
	isn *isn_iter;

	ctx->blk = blk;

	for(isn_iter = block_first_isn(blk); isn_iter; isn_iter = isn_next(isn_iter)){
		isn_iter->flag = true;
	}

	for(isn_iter = block_first_isn(blk); isn_iter; isn_iter = isn_next(isn_iter)){
		if(isn_iter->flag){
			isn_on_live_vals(isn_iter, isn_spill, vctx);
		}
	}
}

void pass_spill(function *fn, struct unit *unit, const struct target *target)
{
	block *entry = function_entry_block(fn, false);
	dynmap *alloc_markers;
	struct spill_ctx ctx = { 0 };

	if(!entry)
		return;

	lifetime_fill_func(fn);

	alloc_markers = BLOCK_DYNMAP_NEW();

	/* FIXME: ctx.spill_space conflicts/overlaps with regalloc's */
	ctx.regcount = target->abi.scratch_regs.count;
	ctx.utl = unit_uniqtypes(unit);
	blocks_traverse(entry, blk_spill, &ctx, alloc_markers);

	dynmap_free(alloc_markers);
}
