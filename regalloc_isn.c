#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "mem.h"
#include "dynmap.h"

#include "regalloc_isn.h"

#include "isn_struct.h"
#include "isn_internal.h"
#include "isn.h"
#include "val_struct.h"
#include "lifetime_struct.h"
#include "function_struct.h"

struct greedy_ctx
{
	block *blk;
	char *in_use;
	int nregs;
	unsigned isn_num;
	unsigned spill_space;
	unsigned ptrsz;
};

static void regalloc_spill(val *v, struct greedy_ctx *ctx)
{
	unsigned vsz = val_size(v, ctx->ptrsz);

	ctx->spill_space += vsz;
	assert(vsz && "unsized name val in regalloc");
}

static void regalloc_greedy1(val *v, isn *isn, void *vctx)
{
	struct greedy_ctx *ctx = vctx;
	struct lifetime *lt;

	(void)isn;

	/* if it lives across blocks we must use memory */
	if(v->live_across_blocks){
		/* optimisation - ensure the value is in the same register for all blocks
		 * mem2reg or something similar should do this */
		regalloc_spill(v, ctx);
		return;
	}

	switch(v->type){
		case NAME:
			break;
		case ARG: /* allocated elsewhere */
		default:
			return; /* not something we need to regalloc */
	}

	lt = dynmap_get(val *, struct lifetime *, block_lifetime_map(ctx->blk), v);
	assert(lt);

	if(lt->start == ctx->isn_num && v->u.addr.u.name.loc.u.reg == -1){
		int i;

		for(i = 0; i < ctx->nregs; i++)
			if(!ctx->in_use[i])
				break;

		if(i == ctx->nregs){
			/* no reg available */
			regalloc_spill(v, ctx);

		}else{
			ctx->in_use[i] = 1;

			v->u.addr.u.name.loc.where = NAME_IN_REG;
			v->u.addr.u.name.loc.u.reg = i;
		}

	}

	if(!v->live_across_blocks
	&& lt->end == ctx->isn_num
	&& v->u.addr.u.name.loc.u.reg >= 0)
	{
		ctx->in_use[v->u.addr.u.name.loc.u.reg] = 0;
	}
}

struct spill_ctx
{
	unsigned offset;
	unsigned ptrsz;
};

static void regalloc_greedy_spill(val *v, isn *isn, void *vctx)
{
	struct spill_ctx *ctx = vctx;
	unsigned size;

	(void)isn;

	if(v->type != NAME
	|| v->u.addr.u.name.loc.where != NAME_IN_REG
	|| v->u.addr.u.name.loc.u.reg != -1)
	{
		return;
	}

	size = val_size(v, ctx->ptrsz);

	v->u.addr.u.name.loc.where = NAME_SPILT;
	v->u.addr.u.name.loc.u.off = ctx->offset + size;

	ctx->offset += size;
}

static void mark_other_block_val_as_used(val *v, isn *isn, void *vctx)
{
	char *in_use = vctx;
	int idx;

	/* isn may be null from mark_arg_vals_as_used() */
	(void)isn;

	switch(v->type){
		case NAME:
			if(v->u.addr.u.name.loc.where == NAME_SPILT)
				return;
			idx = v->u.addr.u.name.loc.u.reg;
			break;
		case ARG:
			if(v->u.arg.loc.where == NAME_SPILT)
				return;
			idx = v->u.arg.loc.u.reg;
			break;
		default:
			return;
	}

	if(idx == -1)
		return;

	in_use[idx] = 1;
}

static void mark_other_block_vals_as_used(char *in_use, isn *isn)
{
	for(; isn; isn = isn->next)
		isn_on_live_vals(isn, mark_other_block_val_as_used, in_use);
}

static void mark_arg_vals_as_used(char *in_use, function *func)
{
	size_t i;

	for(i = 0; i < func->nargs; i++){
		val *arg = &func->args[i].val;

		mark_other_block_val_as_used(arg, NULL, in_use);
	}
}

static void mark_callee_save_as_used(
		char *in_use, const int *callee_save, unsigned callee_save_cnt)
{
	for(; callee_save_cnt > 0; callee_save_cnt--, callee_save++)
		in_use[*callee_save] = 1;
}

static void regalloc_greedy(
		block *blk, function *func, isn *const head,
		const struct backend_traits *backend)
{
	struct greedy_ctx alloc_ctx = { 0 };
	isn *isn_iter;

	alloc_ctx.in_use = xcalloc(backend->nregs, 1);
	alloc_ctx.nregs = backend->nregs;
	alloc_ctx.blk = blk;
	alloc_ctx.ptrsz = backend->ptrsz;

	/* mark scratch as in use */
	alloc_ctx.in_use[backend->scratch_reg] = 1;

	/* mark values who are in use in other blocks as in use */
	mark_other_block_vals_as_used(alloc_ctx.in_use, head);

	mark_arg_vals_as_used(alloc_ctx.in_use, func);

	mark_callee_save_as_used(
			alloc_ctx.in_use,
			backend->callee_save,
			backend->callee_save_cnt);

	for(isn_iter = head; isn_iter; isn_iter = isn_iter->next, alloc_ctx.isn_num++)
		isn_on_live_vals(isn_iter, regalloc_greedy1, &alloc_ctx);

	free(alloc_ctx.in_use);

	/* any leftovers become elems in the regspill alloca block */
	if(alloc_ctx.spill_space){
		struct spill_ctx spill_ctx = { 0 };
		val *spill_alloca = val_alloca();

		spill_ctx.ptrsz = alloc_ctx.ptrsz;

		isn_alloca(blk, alloc_ctx.spill_space, spill_alloca);

		for(isn_iter = head; isn_iter; isn_iter = isn_iter->next)
			isn_on_live_vals(isn_iter, regalloc_greedy_spill, &spill_ctx);
	}
}

static void simple_regalloc(
		block *blk, function *func, const struct backend_traits *backend)
{
	isn *head = block_first_isn(blk);

	regalloc_greedy(blk, func, head, backend);
}

void isn_regalloc(
		block *blk, function *func, const struct backend_traits *backend)
{
	simple_regalloc(blk, func, backend);
}
