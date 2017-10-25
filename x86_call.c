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
#include "lifetime.h"
#include "block_struct.h"
#include "val_struct.h"
#include "val_internal.h"
#include "isn.h"
#include "isn_struct.h"
#include "type.h"

#include "x86_internal.h"
#include "x86_isns.h"
#include "x86_call.h"

/* FIXME: hack */
extern const struct backend_isn x86_isn_call;

typedef struct dep
{
	int target, current; /* register indexes */
	int next, prev; /* order in chain, which should be done first */
	bool done;
} dep;

struct x86_spill_ctx
{
	dynmap *dontspill;
	dynmap *spill;
	block *blk;
	unsigned this_call_stack_use;
	isn *call_isn;
};

static void gather_for_spill(val *v, const struct x86_spill_ctx *ctx)
{
	const struct lifetime *lt;
	bool spill = false;

	if(dynmap_exists(val *, ctx->dontspill, v))
		return;

	if(!val_is_volatile(v))
		return;

	if(v->live_across_blocks)
		spill = true;

	lt = dynmap_get(val *, struct lifetime *, ctx->blk->val_lifetimes, v);
	if(lt /* value lives in this block */
	&& lifetime_contains(lt, ctx->call_isn, false) /* don't spill if the value ends on the isn */)
	{
		spill = true;
	}

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
		case LABEL:
		case BACKEND_TEMP:
		case ALLOCA:
		case ABI_TEMP:
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

		spillctx->this_call_stack_use += type_size(v->ty);

		x86_make_stack_slot(
				&stack_slot,
				octx->stack.current + spillctx->this_call_stack_use,
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
	function_onblocks(func, find_args_in_block, spillctx);
}

static dynmap *x86_spillregs(
		block *blk,
		val *except[],
		isn *call_isn,
		x86_octx *octx)
{
	struct x86_spill_ctx spillctx = { 0 };
	val **vi;

	spillctx.spill     = dynmap_new(val *, NULL, val_hash);
	spillctx.dontspill = dynmap_new(val *, NULL, val_hash);
	spillctx.call_isn = call_isn;
	spillctx.blk = blk;

	for(vi = except; *vi; vi++){
		dynmap_set(val *, void *, spillctx.dontspill, *vi, (void *)NULL);
	}

	gather_live_vals(blk, &spillctx);
	gather_arg_vals(octx->func, &spillctx);

	spill_vals(octx, &spillctx);

	if(spillctx.this_call_stack_use > octx->stack.call_spill_max)
		octx->stack.call_spill_max = spillctx.this_call_stack_use;

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

#if 0
static void topographical_reg_init(dynarray *args, size_t n_reg_args, dep *deps)
{
	size_t i;

	for(i = 0; i < n_reg_args; i++){
		val *varg = dynarray_ent(args, i);
		struct location *loc = val_location(varg);

		deps[i].next = -1;
		deps[i].prev = -1;
		deps[i].done = false;

		assert(i < x86_arg_reg_count);
		deps[i].target = x86_arg_regs[i];

		if(loc && loc->where == NAME_IN_REG){
			deps[i].current = regt_index(loc->u.reg);
		}else{
			deps[i].current = -1;
		}
	}
}

static void topographical_reg_depend(size_t n_reg_args, dep *deps)
{
	size_t i;

	for(i = 0; i < n_reg_args; i++){
		size_t j;

		for(j = 0; j < n_reg_args; j++){
			if(i == j)
				continue;

			if(deps[i].current == deps[j].target){
				assert(deps[i].next == -1
						&& deps[j].prev == -1
						&& "mixup resolving register args");

				deps[i].next = j;
				deps[j].prev = i;
				/* continue for mixup checks */
			}
		}
	}
}

static bool topographical_reg_check_loop(size_t n_reg_args, dep *deps)
{
	size_t i;

	for(i = 0; i < n_reg_args; i++){
		/* pick an entry that isn't done, and follow down its chain.
		 * if the chain count is greater than n_reg_args, we've got a loop
		 */
		size_t count = 0;
		int j;

		for(j = deps[i].next; j != -1; j = deps[j].next){
			count++;

			if(count > n_reg_args)
				return true;
		}
	}

	return false;
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
			dynarray_count(args), x86_arg_reg_count);
	dep *deps = xmalloc(n_reg_args * sizeof *deps);
	size_t i;
	bool chain_loop = false;

	topographical_reg_init(args, n_reg_args, deps);

	topographical_reg_depend(n_reg_args, deps);

	/* check we don't have an infinite dependency chain */
	chain_loop = topographical_reg_check_loop(n_reg_args, deps);

	if(chain_loop){
		/* cyclic dependency, need to start off with a free register */
		val reg;
		val *varg;

		/* if we have a chain-loop then none of the args should be a starting
		 * point, because we have a chain all the way through them
		 *
		 * i.e. deps[0..<n_reg_args].prev != -1
		 */

		assert(deps[0].prev != -1);
		varg = dynarray_ent(args, deps[0].prev);

		x86_comment(octx, "cyclic dependency between arg regs - using scratch");

		/* break dependency chain */
		deps[deps[0].prev].next = -1;
		deps[0].prev = -1;

		octx->scratch_reg_reserved = true;
		x86_make_reg(&reg, SCRATCH_REG, varg->ty);

		/* save argument in scratch for now */
		x86_mov(varg, &reg, octx);
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
			struct location *loc = val_location(arg);
			val reg, scratch_fix;
			val *arg_to_use = arg;

			if(loc && loc->where == NAME_IN_REG){
				assert(deps[j].current == regt_index(loc->u.reg));
			}else{
				assert(deps[j].current == -1);
			}

			/* perform the mov */
			if(chain_loop && deps[j].next == -1){
				/* final entry in the chain - fix this using SCRATCH_REG */
				arg_to_use = &scratch_fix;
				x86_make_reg(&scratch_fix, SCRATCH_REG, arg->ty);
				x86_comment(octx, "restoring final register from scratch");
			}

			x86_make_reg(&reg, deps[j].target, arg->ty);

			x86_mov(arg_to_use, &reg, octx);

			/* finish book-keeping */
			deps[j].done = true;
			if(deps[j].target /* reg index */ == SCRATCH_REG){
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

		if(i < x86_arg_reg_count)
			continue;

		arg = dynarray_ent(args, i);

		(void)arg;
		assert(0 && "TODO: stack args");
	}

	topographical_reg_args_move(args, octx);
}
#endif

void x86_emit_call(
		block *blk, isn *isn,
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

	spilt = x86_spillregs(blk, except, isn, octx);

	/* all regs spilt, can now shift arguments into arg regs */
#if 0
	x86_call_assign_arg_regs(args, octx);
#endif

	if(fn->kind == GLOBAL){
		fprintf(octx->fout,
				"\tcall %s\n",
				global_name_mangled(fn->u.global, unit_target_info(octx->unit)));
	}else{
		emit_isn_operand operand;

		operand.val = fn;
		operand.dereference = false;

		x86_emit_isn(&x86_isn_call, octx, &operand, 1, " *");
	}

	octx->scratch_reg_reserved = false;

	if(into_or_null && !type_is_void(val_type(into_or_null))){
		type *ty = type_func_call(type_deref(fn->ty), NULL, NULL);
		val eax;

		x86_make_eax(&eax, ty);

		x86_mov(&eax, into_or_null, octx);
	}

	x86_restoreregs(spilt, octx);
}
