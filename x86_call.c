#include <stdio.h>
#include <stddef.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "dynarray.h"
#include "dynmap.h"
#include "mem.h"

#include "val.h"
#include "block.h"
#include "function.h"
#include "unit.h"
#include "lifetime_struct.h"
#include "block_struct.h"
#include "val_struct.h"
#include "val_internal.h"
#include "isn_struct.h"
#include "type.h"

#include "x86_internal.h"
#include "x86_isns.h"
#include "x86_call.h"

const int x86_arg_regs[] = {
	4,
	5,
	3,
	2,
	/* TODO: r8, r9 */
};

const unsigned x86_arg_reg_count = countof(x86_arg_regs);


struct x86_spill_ctx
{
	dynmap *alloca2stack;
	dynmap *dontspill;
	dynmap *spill;
	block *blk;
	unsigned long spill_alloca;
	unsigned call_isn_idx;
};

static void gather_for_spill(val *v, const struct x86_spill_ctx *ctx)
{
	const struct lifetime lt_inf = LIFETIME_INIT_INF;
	const struct lifetime *lt;
	bool spill = false;

	if(dynmap_exists(val *, ctx->dontspill, v))
		return;

	if(!val_is_volatile(v))
		return;

	lt = dynmap_get(val *, struct lifetime *, ctx->blk->val_lifetimes, v);
	if(!lt)
		lt = &lt_inf;

	if(v->live_across_blocks)
		spill = true;

	/* don't spill if the value ends on the isn */
	if(lt->start <= ctx->call_isn_idx && ctx->call_isn_idx < lt->end)
		spill = true;

	if(spill){
		dynmap_set(val *, void *, ctx->spill, v, (void *)NULL);
	}
}

static void maybe_gather_for_spill(val *v, isn *isn, void *vctx)
{
	const struct x86_spill_ctx *ctx = vctx;

	(void)isn;

	switch(v->kind){
		case LITERAL:
		case GLOBAL:
		case BACKEND_TEMP:
		case ALLOCA:
			return; /* no need to spill */

		case FROM_ISN:
		case ARGUMENT:
			break;
	}

	gather_for_spill(v, ctx);
}

static void gather_live_vals(block *blk, struct x86_spill_ctx *spillctx)
{
	isn *isn;
	for(isn = block_first_isn(blk); isn; isn = isn->next){
		if(isn->skip)
			continue;

		isn_on_live_vals(isn, maybe_gather_for_spill, spillctx);
	}
}

static void spill_vals(x86_octx *octx, struct x86_spill_ctx *spillctx)
{
	size_t idx;
	val *v;

	for(idx = 0; (v = dynmap_key(val *, spillctx->spill, idx)); idx++){
		val stack_slot = { 0 };

		spillctx->spill_alloca += type_size(v->ty);

		x86_make_stack_slot(
				&stack_slot,
				octx->alloca_bottom + spillctx->spill_alloca,
				v->ty);

		x86_comment(octx, "spill '%s'", val_str(v));

		x86_mov_deref(v, &stack_slot, octx, false, true);

		assert(stack_slot.kind == BACKEND_TEMP);
		assert(stack_slot.u.temp_loc.where == NAME_SPILT);
		dynmap_set(
				val *, uintptr_t,
				spillctx->spill,
				v,
				(uintptr_t)stack_slot.u.temp_loc.u.off);
	}
}

static void find_args_in_isn(val *v, isn *isn, void *vctx)
{
	struct x86_spill_ctx *spillctx = vctx;

	(void)isn;

	if(v->kind != ARGUMENT)
		return;

	gather_for_spill(v, spillctx);
}

static void find_args_in_block(block *blk, void *vctx)
{
	struct x86_spill_ctx *spillctx = vctx;
	isn *isn;

	for(isn = block_first_isn(blk); isn; isn = isn->next){
		isn_on_live_vals(isn, find_args_in_isn, spillctx);
	}
}

static void gather_arg_vals(function *func, struct x86_spill_ctx *spillctx)
{
	/* can't just spill regs in this block, need to spill 'live' regs,
	 * e.g.  argument regs
	 * we only spill arguments that are actually used, for the moment,
	 * that's any argument used at all, regardless of blocks
	 */
	dynmap *markers = BLOCK_DYNMAP_NEW();
	block *blk = function_entry_block(func, false);
	assert(blk);

	blocks_traverse(blk, find_args_in_block, spillctx, markers);

	dynmap_free(markers);
}

static dynmap *x86_spillregs(
		block *blk,
		val *except[],
		unsigned call_isn_idx,
		x86_octx *octx)
{
	struct x86_spill_ctx spillctx = { 0 };
	val **vi;

	spillctx.spill     = dynmap_new(val *, NULL, val_hash);
	spillctx.dontspill = dynmap_new(val *, NULL, val_hash);
	spillctx.alloca2stack = octx->alloca2stack;
	spillctx.call_isn_idx = call_isn_idx;
	spillctx.blk = blk;

	for(vi = except; *vi; vi++){
		dynmap_set(val *, void *, spillctx.dontspill, *vi, (void *)NULL);
	}

	gather_live_vals(blk, &spillctx);
	gather_arg_vals(octx->func, &spillctx);

	spill_vals(octx, &spillctx);

	if(spillctx.spill_alloca > octx->spill_alloca_max)
		octx->spill_alloca_max = spillctx.spill_alloca;

	dynmap_free(spillctx.dontspill);
	return spillctx.spill;
}

static void x86_restoreregs(dynmap *regs, x86_octx *octx)
{
	size_t idx;
	val *v;

	for(idx = 0; (v = dynmap_key(val *, regs, idx)); idx++){
		unsigned off = dynmap_value(uintptr_t, regs, idx);
		val stack_slot = { 0 };

		x86_comment(octx, "restore '%s'", val_str(v));

		x86_make_stack_slot(&stack_slot, off, v->ty);

		x86_mov_deref(&stack_slot, v, octx, true, false);
	}

	dynmap_free(regs);
}

static void topographical_reg_args_move(dynarray *args, x86_octx *octx)
{
	/*
	 * mov x(%rip), %eax
	 * mov x(%rip), %edx
	 * mov x(%rip), %edi
	 * mov x(%rip), %esi
	 * mov %eax, %edi
	 * mov %edx, %esi
	 * mov %edi, %edx
	 * mov %esi, %ecx
	 * call ...
	 *
	 * "->" means depends on
	 * edi -> eax
	 * esi -> edx
	 * edx -> edi
	 * ecx -> esi
	 *
	 * ecx -> esi -> edx -> edi -> eax
	 *
	 * have eax, so we do reverse order for mov:ing
	 */
	const size_t n_reg_args = MIN(
			dynarray_count(args), countof(x86_arg_regs));
	typedef struct dep
	{
		int needs, needed; /* register indexes */
		int next, prev;
		bool done;
	} dep;
	dep *deps = xmalloc(n_reg_args * sizeof *deps);
	size_t i;

	/* initialise */
	for(i = 0; i < n_reg_args; i++){
		val *varg = dynarray_ent(args, i);
		struct name_loc *loc = val_location(varg);

		deps[i].next = -1;
		deps[i].prev = -1;
		deps[i].done = false;

		assert(i < countof(x86_arg_regs));
		deps[i].needs = x86_arg_regs[i];

		if(loc && loc->where == NAME_IN_REG){
			deps[i].needed = loc->u.reg;
		}else{
			deps[i].needed = -1;
		}
	}

	/* dependency generation */
	for(i = 0; i < n_reg_args; i++){
		size_t j;

		if(deps[i].done)
			continue;

		for(j = 0; j < n_reg_args; j++){
			if(i == j)
				continue;
			if(deps[j].done)
				continue;

			if(deps[i].needed == deps[j].needs){
				assert(deps[i].next == -1
						&& deps[j].prev == -1
						&& "cyclic reference resolving register args");

				deps[i].next = j;
				deps[j].prev = i;
				/* continue for cyclic reference check */
			}
		}
	}

	/* need to do all the deps with -1 as their prev,
	 * as they have no registers they need to rely on.
	 *
	 * then we walk down all of the next chains performing the mov:s
	 * until we have exhausted everything.
	 */

	for(i = 0; i < n_reg_args; i++){
		int j;

		if(deps[i].done)
			continue;
		if(deps[i].prev != -1)
			continue;

		/* walk the dep chain */
		for(j = i; j != -1; j = deps[j].next){
			val *arg = dynarray_ent(args, j);
			struct name_loc *loc = val_location(arg);
			val reg;

			if(loc && loc->where == NAME_IN_REG){
				assert(deps[j].needed == loc->u.reg);
			}else{
				assert(deps[j].needed == -1);
			}

			x86_make_reg(&reg, deps[j].needs, arg->ty);

			x86_mov(arg, &reg, octx);

			deps[j].done = true;

			if(deps[j].needs /* reg index */ == SCRATCH_REG){
				octx->scratch_reg_reserved = true;
			}
		}
	}

	for(i = 0; i < n_reg_args; i++){
		assert(deps[i].done);
	}

	free(deps);
}

static void x86_call_assign_arg_regs(dynarray *args, x86_octx *octx)
{
	size_t i;

	/* do stack moves first, before we reserve the scratch_reg */
	dynarray_iter(args, i){
		val *arg;

		if(i < countof(x86_arg_regs))
			continue;

		arg = dynarray_ent(args, i);

		assert(0 && "TODO: stack args");
	}

	topographical_reg_args_move(args, octx);
}

void x86_emit_call(
		block *blk, unsigned isn_idx,
		val *into_or_null, val *fn,
		dynarray *args,
		x86_octx *octx)
{
	val *except[3];
	dynmap *spilt;

	except[0] = fn;
	except[1] = into_or_null;
	except[2] = NULL;

	octx->max_align = 16; /* ensure 16-byte alignment for calls */

	spilt = x86_spillregs(blk, except, isn_idx, octx);

	/* all regs spilt, can now shift arguments into arg regs */
	x86_call_assign_arg_regs(args, octx);

	if(fn->kind == GLOBAL){
		fprintf(octx->fout, "\tcall %s\n", global_name(fn->u.global));
	}else{
		emit_isn_operand operand;

		operand.val = fn;
		operand.dereference = false;

		x86_emit_isn(&x86_isn_call, octx, &operand, 1, " *");
	}

	octx->scratch_reg_reserved = false;

	if(into_or_null){
		type *ty = type_func_call(type_deref(fn->ty), NULL, NULL);
		val eax;

		x86_make_eax(&eax, ty);

		x86_mov(&eax, into_or_null, octx);
	}

	x86_restoreregs(spilt, octx);
}
