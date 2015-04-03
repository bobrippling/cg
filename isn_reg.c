#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "mem.h"

#include "isn_reg.h"

#include "isn_struct.h"
#include "isn_internal.h"
#include "isn.h"
#include "val_struct.h"
#include "block_internal.h"

#define VAL_REG(v) (v)->u.addr.u.name.reg

struct lifetime
{
	unsigned start, end;
};

static void init_regalloc_data(val *v)
{
	v->pass_data = xmalloc(sizeof(struct lifetime));
}

static void free_regalloc_data1(val *v, isn *isn, void *ctx)
{
	(void)ctx;
	(void)isn;
	free(v->pass_data);
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
	struct lifetime *lt = v->pass_data;

	(void)isn;

	if(v->type != NAME)
		return;

	if(!lt){
		init_regalloc_data(v);

		lt = v->pass_data;
		assert(lt);
		lt->start = isn_count;
	}

	lt->end = isn_count;
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
	const struct lifetime *lt = v->pass_data;
	struct greedy_ctx *ctx = vctx;

	(void)isn;

	if(!lt)
		return; /* not something we need to regalloc */

	assert(v->type == NAME);

	if(lt->start == ctx->isn_num && VAL_REG(v) == -1){
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

	if(lt->end == ctx->isn_num && VAL_REG(v) >= 0){
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

static void regalloc_greedy(block *blk, isn *head, int nregs)
{
	struct greedy_ctx alloc_ctx = { 0 };
	int i;

	alloc_ctx.in_use = xcalloc(nregs, 1);
	alloc_ctx.nregs = nregs;

	for(; head; head = head->next, alloc_ctx.isn_num++)
		isn_on_vals(head, regalloc_greedy1, &alloc_ctx);

	for(i = 0; i < nregs; i++)
		assert(alloc_ctx.in_use[i] == 0);

	free(alloc_ctx.in_use);

	/* any leftovers become elems in the regspill alloca block */
	if(alloc_ctx.spill_space){
		struct spill_ctx spill_ctx = { 0 };
		val *spill_alloca = val_alloca();

		isn_alloca(blk, alloc_ctx.spill_space, spill_alloca);

		for(; head; head = head->next)
			isn_on_vals(head, regalloc_greedy_spill, &spill_ctx);
	}
}

static void simple_regalloc(block *blk, int nregs)
{
	isn *head = block_first_isn(blk);

	assign_lifetimes(head);

	regalloc_greedy(blk, head, nregs);

	free_regalloc_data(head);
}

void isn_regalloc(block *blk, int nregs)
{
	simple_regalloc(blk, nregs);
}
