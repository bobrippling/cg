#include <stdio.h>
#include <stdint.h>
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

#define REGALLOC_VERBOSITY 0 /* 0 - 3 */
#define SHOW_STACKALLOC 0

#define MAP_GUARDED_VALS 0

struct greedy_ctx
{
	block *blk;
	const struct regset *scratch_regs;
	uniq_type_list *utl;
	dynmap *alloced_vars;
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
		case LABEL:
		case UNDEF:
			return false;

		case BACKEND_TEMP:
			assert(0 && "BACKEND_TEMP unreachable at this stage");

		case ALLOCA:
		case ARGUMENT:
		case FROM_ISN:
		case ABI_TEMP:
		{
			struct location *loc = val_location(v);
			switch(loc->where){
				case NAME_SPILT:
				case NAME_IN_REG:
					return false;
				case NAME_NOWHERE:
				case NAME_IN_REG_ANY:
					break;
			}
			break;
		}
	}
	return !type_is_void(val_type(v));
}

static void regalloc_debug(val *v, bool is_fp, struct lifetime *lt, struct greedy_ctx *ctx)
{
	struct isn *isn_iter;

	fprintf(stderr, "pre-regalloc %s:\n", val_str(v));

	for(isn_iter = lt->start; isn_iter; isn_iter = isn_next(isn_iter)){
		const char *space = "";
		unsigned i;
		dynarray *clobbers = isn_clobbers(isn_iter);

		fprintf(stderr, "  [%p]: ", isn_iter);

		for(i = 0; i < ctx->scratch_regs->count; i++){
			const regt reg = regt_make(regset_get(ctx->scratch_regs, i), is_fp);
			if(regset_is_marked(isn_iter->regusemarks, reg)){
				fprintf(stderr, "%s<reg %d>", space, reg);
				space = ", ";
			}
		}

		if(!dynarray_is_empty(clobbers)){
			fprintf(stderr, " (clobbers:");
			space = "";

			dynarray_iter(clobbers, i){
				regt reg = (regt)(uintptr_t)dynarray_ent(clobbers, i);
				fprintf(stderr, "%s <reg %d>", space, reg);
				space = ",";
			}
			fprintf(stderr, ")");
		}

		fprintf(stderr, " [%s]\n", isn_type_to_str(isn_iter->type));

		if(isn_iter == lt->end)
			break;
	}
}

static bool reg_free_during(regt reg, unsigned *const priority, struct lifetime *lt, val *for_val)
{
	struct isn *isn_iter;
	struct isn *current_implicit_use = NULL;

	for(isn_iter = lt->start; isn_iter; isn_iter = isn_next(isn_iter)){
		if(regset_is_marked(isn_iter->regusemarks, reg)){
			/*
			 * Register is in use - could we still use it for `for_val` ?
			 *
			 * $a = ...
			 * ...
			 * $arg1<abi 1> = $a
			 * $r = call $f($arg1<abi 1>)
			 *
			 * We can reuse <abi 1> if it's only in use during $a's lifetime as a
			 * trivial copy.
			 *
			 * In short: if the current isn is an implicit_use, we can ignore any
			 * trivial copies of `for_val` until we hit the end of the implicit_use.
			 * Upon the end of the implicit use, `for_val`'s lifetime should end, or
			 * the candidate register (`reg`) shouldn't be used.
			 */

			if(isn_iter->type == ISN_IMPLICIT_USE_START){
				assert(!current_implicit_use);
				current_implicit_use = isn_iter;

			}else if(isn_iter->type == ISN_IMPLICIT_USE_END){
				assert(current_implicit_use);
				current_implicit_use = NULL;

			}else if(!current_implicit_use){
				if(REGALLOC_VERBOSITY > 2)
					fprintf(stderr, "  rejected - not inside implicit use (%p)\n", (void *)isn_iter);
				return false;

			}else if(isn_iter->type == ISN_COPY){
				bool is_copy_to_specific_reg = val_is_reg_specific(isn_iter->u.copy.to, reg);

				if(is_copy_to_specific_reg){
					if(isn_iter->u.copy.from == for_val){
						/* fine */
						*priority = 2; /* no transfer required, better to pick this */
						if(REGALLOC_VERBOSITY > 2){
							fprintf(stderr, "  isn %s (%p) is a copy from %s to reg %#x\n",
									isn_type_to_str(isn_iter->type), (void *)isn_iter, val_str(for_val), reg);
						}

					}else{
						/* copy to our-reg, not from our val - we can't use `reg` for `for_val` */
						return false;
					}
				}else{
					/* copy to not-our-reg - not interested */
				}

			}else if(!isn_is_noop(isn_iter) && isn_vals_has(isn_iter, for_val)){
				if(REGALLOC_VERBOSITY > 2)
					fprintf(stderr, "  rejected - not a noop (%p)\n", (void *)isn_iter);
				return false;
			}

			if(REGALLOC_VERBOSITY > 2)
				fprintf(stderr,
						"  (%s: technically in use - only in abi copies inside implicit use blocks)\n",
						isn_type_to_str(isn_iter->type));

			/* continue */
		}

		if(isn_iter == lt->end)
			break;
	}

	return true;
}

static bool reg_in_non_implicituse_during(regt reg, struct lifetime *lt)
{
	struct isn *isn_iter;

	for(isn_iter = lt->start; isn_iter; isn_iter = isn_next(isn_iter)){
		if(regset_is_marked(isn_iter->regusemarks, reg)
		&& isn_iter->type != ISN_IMPLICIT_USE_START
		&& isn_iter->type != ISN_IMPLICIT_USE_END)
		{
			return true;
		}

		if(isn_iter == lt->end)
			break;
	}

	return false;
}

static void regalloc_val_noupdate(
		val *v,
		struct location *val_locn,
		struct lifetime *lt,
		struct greedy_ctx *ctx)
{
	const bool is_fp = type_is_float(val_type(v), 1);
	unsigned i;
	unsigned freecount = 0;
	struct regsearch {
		regt reg;
		unsigned priority;
	} foundreg;

	foundreg.reg = regt_make_invalid();
	foundreg.priority = 0;

	if(REGALLOC_VERBOSITY > 1)
		regalloc_debug(v, is_fp, lt, ctx);

	for(i = 0; i < ctx->scratch_regs->count; i++){
		struct regsearch search;

		search.reg = regt_make(ctx->scratch_regs->regs[i], is_fp);
		search.priority = 1;

		if(REGALLOC_VERBOSITY > 2)
			fprintf(stderr, "val %s, candidate reg %#x...\n", val_str(v), search.reg);

		if(reg_free_during(search.reg, &search.priority, lt, v)){
			bool overwrite = search.priority > foundreg.priority;

			if(REGALLOC_VERBOSITY > 2){
				fprintf(stderr, "  reg %#x is free - %s priority (%u)\n",
						search.reg,
						overwrite ? "higher" : "lower",
						search.priority);
			}

			if(overwrite)
				foundreg = search;
			freecount++;
		}
	}

	if(REGALLOC_VERBOSITY)
		fprintf(stderr, "regalloc(%s) => reg %#x\n", val_str(v), foundreg.reg);

	assert(regt_is_valid(foundreg.reg));
	assert(freecount > 0);

	val_locn->where = NAME_IN_REG;
	val_locn->u.reg = foundreg.reg;
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

	if(isn_is_implicituse(isn->type))
		return;

	if(!regalloc_applies_to(v))
		return;

	switch(v->kind){
		case LITERAL:
		case GLOBAL:
		case LABEL:
		case BACKEND_TEMP:
		case UNDEF:
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
		/* handled in spill pass */
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
		struct location *abi_locn;
		struct lifetime after_abi_lifetime;

		after_abi_lifetime.start = lt->start->next;
		after_abi_lifetime.end = lt->end;

		/* optimisation: $arg = <abi ...> - don't touch the abi assignment and keep the reg */
		if(isn->type == ISN_COPY
		&& isn->u.copy.from->kind == ABI_TEMP
		&& (abi_locn = val_location(isn->u.copy.from))
		&& abi_locn->where == NAME_IN_REG
		&& !reg_in_non_implicituse_during(abi_locn->u.reg, &after_abi_lifetime))
		/* ^ if the register's in use by something else (likely already having used this optimisation), ignore */
		{
			assert(regset_mark_count(isn->regusemarks, abi_locn->u.reg) == 1
					&& "the register should be in-use just by the abi-assignment isn");

			memcpy(val_locn, abi_locn, sizeof(*val_locn));
			if(REGALLOC_VERBOSITY){
				fprintf(stderr, "regalloc(%s) => reg %#x (mirroring abi reg %s)\n",
						val_str(v), val_locn->u.reg, val_str_rn(0, isn->u.copy.from));
			}
		}else{
			regalloc_val_noupdate(v, val_locn, lt, ctx);
		}

		assert(val_locn->where == NAME_IN_REG);
		mark_in_use_isns(val_locn->u.reg, lt, false);
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

static void mark_isn_clobbers(isn *isn)
{
	dynarray *clobbers = isn_clobbers(isn);
	size_t i;

	dynarray_iter(clobbers, i){
		regt reg = (regt)(uintptr_t)dynarray_ent(clobbers, i);
		struct lifetime lt;

		lt.start = isn;
		lt.end = isn;

		mark_in_use_isns(reg, &lt, true);
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
		mark_isn_clobbers(isn_iter);
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

	function_onblocks(fn, blk_regalloc_pass, &ctx);

	dynmap_free(alloc_markers);
}
