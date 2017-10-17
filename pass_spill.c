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
	function *fn;
	unsigned used_count;
	unsigned regcount;
};

static void spill_assign(val *spill, struct spill_ctx *ctx)
{
	struct location *spill_loc = val_location(spill);

	assert(spill_loc->where == NAME_NOWHERE);

	spill_loc->where = NAME_SPILT;
	spill_loc->u.off = function_alloc_stack_space(ctx->fn, type_deref(val_type(spill)));
}

static void spill(val *v, isn *use_isn, struct spill_ctx *ctx)
{
	type *const ty = val_type(v);
	val *spill;
	struct lifetime *spill_lt = xmalloc(sizeof *spill_lt);
	struct lifetime *v_lt = dynmap_get(val *, struct lifetime *, block_lifetime_map(ctx->blk), v);
	isn *alloca;
	const char *name = val_frontend_name(v);

	if(name){
		spill = val_new_localf(
				type_get_ptr(ctx->utl, ty),
				true,
				"spill.for.%s",
				name);
	}else{
		spill = val_new_localf(
				type_get_ptr(ctx->utl, ty),
				true,
				"spill.%d",
				/*something unique:*/(int)v);
	}

	alloca = isn_alloca(spill);

	spill_assign(spill, ctx);

	memcpy(spill_lt, v_lt, sizeof(*spill_lt));
	dynmap_set(val *, struct lifetime *, block_lifetime_map(ctx->blk), spill, spill_lt);

	isn_insert_before(use_isn, alloca);

	/* no reg overlap - we just setup the values, regalloc can deal with the rest */
	isn_replace_uses_with_load_store(v, spill, use_isn, ctx->fn);
}

static void isn_spill(val *v, isn *isn, void *vctx)
{
	struct spill_ctx *ctx = vctx;
	const struct lifetime *lt;

	if(v->kind == ALLOCA && isn->type == ISN_ALLOCA){
		spill_assign(v, ctx);
		return;
	}

	if(!regalloc_applies_to(v))
		return;

	if(v->live_across_blocks){
		/* optimisation - ensure the value is in the same register for all blocks
		 * mem2reg or something similar should do this */
		if(isn_defines_val(isn, v))
			spill(v, isn, ctx);
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
		ctx->used_count++;

		if(ctx->used_count >= ctx->regcount - 1
		&& !val_is_abi_reg(v))
		{
			spill(v, isn, ctx);
		}

	}else if(lt->end == isn){
		ctx->used_count--;
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
	struct spill_ctx ctx = { 0 };

	if(!entry)
		return;

	ctx.fn = fn;

	lifetime_fill_func(fn);

	ctx.regcount = target->abi.scratch_regs.count;
	ctx.utl = unit_uniqtypes(unit);
	function_onblocks(fn, blk_spill, &ctx);
}
