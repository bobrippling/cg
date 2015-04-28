#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "mem.h"

#include "isn_reg.h"

#include "isn_struct.h"
#include "isn_internal.h"
#include "isn.h"
#include "val_struct.h"

#define VAL_REG(v) (v)->u.addr.u.name.loc.u.reg

static void init_regalloc_data(val *v)
{
	v->pass_data = /*anything non-null:*/&init_regalloc_data;
}

static void free_regalloc_data1(val *v, isn *isn, void *ctx)
{
	(void)ctx;
	(void)isn;
	v->pass_data = NULL;
}

static void free_regalloc_data(isn *head)
{
	for(; head; head = head->next)
		isn_on_vals(head, free_regalloc_data1, NULL);
}

static void assign_lifetime(val *v, isn *isn, void *ctx)
{
	const unsigned isn_count = *(unsigned *)ctx;

	(void)isn;

	if(v->type != NAME)
		return;

	if(!v->pass_data){
		init_regalloc_data(v);

		v->lifetime.start = isn_count;
	}

	v->lifetime.end = isn_count;
}

static void assign_lifetimes(isn *head)
{
	unsigned isn_count = 0;

	for(; head; head = head->next, isn_count++)
		isn_on_vals(head, assign_lifetime, &isn_count);
}

struct greedy_ctx
{
	char *in_use;
	int nregs;
	unsigned isn_num;
	unsigned spill_space;
};

static void regalloc_greedy1(val *v, isn *isn, void *vctx)
{
	struct greedy_ctx *ctx = vctx;

	(void)isn;

	if(!v->pass_data)
		return; /* not something we need to regalloc */

	assert(v->type == NAME);

	if(v->lifetime.start == ctx->isn_num && VAL_REG(v) == -1){
		int i;

		for(i = 0; i < ctx->nregs; i++)
			if(!ctx->in_use[i])
				break;

		if(i == ctx->nregs){
			/* no reg available */
			unsigned vsz = val_size(v);

			ctx->spill_space += vsz;
			assert(vsz && "unsized name val in regalloc");

		}else{
			ctx->in_use[i] = 1;
			VAL_REG(v) = i;
		}

	}

	if(v->lifetime.end == ctx->isn_num && VAL_REG(v) >= 0){
		ctx->in_use[VAL_REG(v)] = 0;
	}
}

struct spill_ctx
{
	unsigned offset;
};

static void regalloc_greedy_spill(val *v, isn *isn, void *vctx)
{
	struct spill_ctx *ctx = vctx;
	unsigned size;

	(void)isn;

	if(v->type != NAME || VAL_REG(v) != -1)
		return;

	size = val_size(v);

	v->u.addr.u.name.loc.where = NAME_SPILT;
	v->u.addr.u.name.loc.u.off = ctx->offset + size;

	ctx->offset += size;
}

static void regalloc_greedy(
		block *blk, isn *const head,
		const struct regalloc_ctx *regs_ctx)
{
	struct greedy_ctx alloc_ctx = { 0 };
	int i;
	isn *isn_iter;

	alloc_ctx.in_use = xcalloc(regs_ctx->nregs, 1);
	alloc_ctx.nregs = regs_ctx->nregs;

	alloc_ctx.in_use[regs_ctx->scratch_reg] = 1;

	for(isn_iter = head; isn_iter; isn_iter = isn_iter->next, alloc_ctx.isn_num++)
		isn_on_vals(isn_iter, regalloc_greedy1, &alloc_ctx);

	for(i = 0; i < regs_ctx->nregs; i++){
		int expected = 0;
		if(i == regs_ctx->scratch_reg)
			expected = 1;
		assert(alloc_ctx.in_use[i] == expected);
	}

	free(alloc_ctx.in_use);

	/* any leftovers become elems in the regspill alloca block */
	if(alloc_ctx.spill_space){
		struct spill_ctx spill_ctx = { 0 };
		val *spill_alloca = val_alloca();

		isn_alloca(blk, alloc_ctx.spill_space, spill_alloca);

		for(isn_iter = head; isn_iter; isn_iter = isn_iter->next)
			isn_on_vals(isn_iter, regalloc_greedy_spill, &spill_ctx);
	}
}

static void simple_regalloc(block *blk, const struct regalloc_ctx *ctx)
{
	isn *head = block_first_isn(blk);

	assign_lifetimes(head);

	regalloc_greedy(blk, head, ctx);

	free_regalloc_data(head);
}

void isn_regalloc(block *blk, const struct regalloc_ctx *ctx)
{
	simple_regalloc(blk, ctx);
}
