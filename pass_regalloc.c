#include <stdio.h>
#include <assert.h>

#include "dynmap.h"
#include "mem.h"

#include "function.h"

#include "pass_regalloc.h"
#include "pass_spill.h"

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
#define SHOW_REGALLOC_VERBOSE 0
#define SHOW_STACKALLOC 0

#define MAP_GUARDED_VALS 0

struct greedy_ctx
{
	block *blk;
	const struct regset *scratch_regs;
	uniq_type_list *utl;
	dynarray spill_isns;
	dynmap *alloced_vars;
	bool spilt;
};

struct regalloc_ctx
{
	const struct target *target;
	uniq_type_list *utl;
};

static void mark_in_use_isns(regt reg, struct lifetime *lt, bool saturate_only)
{
	isn *i;

	for(i = lt->start; i; i = isn_next(i)){
		if(!saturate_only || !regset_is_marked(i->regusemarks, reg))
			regset_mark(i->regusemarks, reg, true);

		if(i == lt->end)
			break;
	}
}

bool regalloc_applies_to(val *v)
{
	switch(v->kind){
		case LITERAL:
		case GLOBAL:
			return false;

		case BACKEND_TEMP:
			assert(0 && "BACKEND_TEMP unreachable at this stage");

		case ALLOCA:
		case ARGUMENT:
		case FROM_ISN:
		case ABI_TEMP:
			break;
	}
	return true;
}

static void regalloc_debug(val *v, bool is_fp, struct lifetime *lt, struct greedy_ctx *ctx)
{
	struct isn *isn_iter;

	fprintf(stderr, "pre-regalloc %s:\n", val_str(v));

	for(isn_iter = lt->start; isn_iter; isn_iter = isn_next(isn_iter)){
		const char *space = "";
		unsigned i;
		fprintf(stderr, "  [%p]: ", isn_iter);

		for(i = 0; i < ctx->scratch_regs->count; i++){
			const regt reg = regt_make(ctx->scratch_regs->regs[i], is_fp);
			if(regset_is_marked(isn_iter->regusemarks, reg)){
				fprintf(stderr, "%s<reg %d>", space, reg);
				space = ", ";
			}
		}
		fprintf(stderr, "\n");

		if(isn_iter == lt->end)
			break;
	}
}

static void regalloc_val(
		val *v,
		struct location *val_locn,
		struct lifetime *lt,
		struct greedy_ctx *ctx)
{
	const bool is_fp = type_is_float(val_type(v), 1);
	unsigned i;
	unsigned freecount = 0;
	regt foundreg = regt_make_invalid();

	if(SHOW_REGALLOC_VERBOSE)
		regalloc_debug(v, is_fp, lt, ctx);

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

	assert(regt_is_valid(foundreg));
	assert(freecount > 0);

	mark_in_use_isns(foundreg, lt, false);

	if(SHOW_REGALLOC)
		fprintf(stderr, "regalloc(%s) => reg %#x\n", val_str(v), foundreg);

	val_locn->where = NAME_IN_REG;
	val_locn->u.reg = foundreg;
}

static void regalloc_greedy1(val *v, isn *isn, void *vctx)
{
	struct greedy_ctx *ctx = vctx;
	struct lifetime *lt;
	struct location *val_locn;
	val *spill = NULL;

	if(MAP_GUARDED_VALS){
		if(dynmap_get(val *, long, ctx->alloced_vars, v))
			return;
		dynmap_set(val *, long, ctx->alloced_vars, v, 1L);
	}

	if(isn->type == ISN_IMPLICIT_USE)
		return;

	if(!regalloc_applies_to(v))
		return;

	switch(v->kind){
		case LITERAL:
		case GLOBAL:
		case BACKEND_TEMP:
			assert(0 && "unreachable");

		case ALLOCA:
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
		regalloc_val(v, val_locn, lt, ctx);
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
		mark_in_use_isns(loc->u.reg, lt, true);
	}
}

static void mark_callee_save_as_used(isn *begin, const struct regset *callee_saves)
{
	struct lifetime all;
	size_t i;

	all.start = begin;
	all.end = NULL;

	for(i = 0; i < callee_saves->count; i++)
		mark_in_use_isns(regset_get(callee_saves, i), &all, true);
}

static void blk_regalloc_pass(block *blk, void *vctx)
{
	struct regalloc_ctx *ctx = vctx;
	struct greedy_ctx alloc_ctx = { 0 };
	isn *head = block_first_isn(blk);
	isn *isn_iter;

	alloc_ctx.blk = blk;
	alloc_ctx.scratch_regs = &ctx->target->abi.scratch_regs;
	alloc_ctx.utl = ctx->utl;
	if(MAP_GUARDED_VALS)
		alloc_ctx.alloced_vars = dynmap_new(val *, NULL, val_hash);

	mark_callee_save_as_used(head, &ctx->target->abi.callee_saves);

	/* pre-scan - mark any existing abi regs as used across their lifetime */
	for(isn_iter = head; isn_iter; isn_iter = isn_next(isn_iter)){
		isn_on_live_vals(isn_iter, regalloc_greedy_pre, &alloc_ctx);
	}

	for(isn_iter = head; isn_iter; isn_iter = isn_next(isn_iter)){
		isn_on_live_vals(isn_iter, regalloc_greedy1, &alloc_ctx);
	}

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

	dynmap_free(alloc_markers);
}
