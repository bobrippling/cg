#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "mem.h"
#include "dynmap.h"

#include "isn_reg.h"

#include "isn_struct.h"
#include "isn_internal.h"
#include "isn.h"
#include "val_struct.h"
#include "lifetime_struct.h"

#define VAL_REG(v) (v)->u.addr.u.name.loc.u.reg

struct greedy_ctx
{
	block *blk;
	char *in_use;
	int nregs;
	unsigned isn_num;
	unsigned spill_space;
	unsigned ptrsz;
};

static void regalloc_greedy1(val *v, isn *isn, void *vctx)
{
	struct greedy_ctx *ctx = vctx;
	struct lifetime *lt;

	(void)isn;

	if(v->type != NAME)
		return; /* not something we need to regalloc */

	lt = dynmap_get(val *, struct lifetime *, block_lifetime_map(ctx->blk), v);
	assert(lt);

	if(lt->start == ctx->isn_num && VAL_REG(v) == -1){
		int i;

		for(i = 0; i < ctx->nregs; i++)
			if(!ctx->in_use[i])
				break;

		if(i == ctx->nregs){
			/* no reg available */
			unsigned vsz = val_size(v, ctx->ptrsz);

			ctx->spill_space += vsz;
			assert(vsz && "unsized name val in regalloc");

		}else{
			ctx->in_use[i] = 1;
			VAL_REG(v) = i;
		}

	}

	if(!v->live_across_blocks && lt->end == ctx->isn_num && VAL_REG(v) >= 0){
		ctx->in_use[VAL_REG(v)] = 0;
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

	if(v->type != NAME || VAL_REG(v) != -1)
		return;

	size = val_size(v, ctx->ptrsz);

	v->u.addr.u.name.loc.where = NAME_SPILT;
	v->u.addr.u.name.loc.u.off = ctx->offset + size;

	ctx->offset += size;
}

static void mark_other_block_val_as_used(val *v, isn *isn, void *vctx)
{
	char *in_use = vctx;
	int idx;

	(void)isn;

	if(v->type != NAME)
		return;

	idx = VAL_REG(v);
	if(idx == -1)
		return;

	in_use[idx] = 1;
}

static void mark_other_block_vals_as_used(char *in_use, isn *isn)
{
	for(; isn; isn = isn->next)
		isn_on_live_vals(isn, mark_other_block_val_as_used, in_use);
}

static void mark_callee_save_as_used(
		char *in_use, const int *callee_save, unsigned callee_save_cnt)
{
	for(; callee_save_cnt > 0; callee_save_cnt--, callee_save++)
		in_use[*callee_save] = 1;
}

static void regalloc_greedy(
		block *blk, isn *const head,
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

static void simple_regalloc(block *blk, const struct backend_traits *backend)
{
	isn *head = block_first_isn(blk);

	regalloc_greedy(blk, head, backend);
}

void isn_regalloc(block *blk, const struct backend_traits *backend)
{
	simple_regalloc(blk, backend);
}
