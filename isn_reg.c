#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "mem.h"

#include "isn_reg.h"

#include "isn_struct.h"
#include "isn_internal.h"
#include "val_struct.h"

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
};

static void regalloc_greedy1(val *v, isn *isn, void *vctx)
{
	const struct lifetime *lt = v->pass_data;
	const struct greedy_ctx *ctx = vctx;

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
		}else{
			ctx->in_use[i] = 1;
			VAL_REG(v) = i;
		}

	}

	if(lt->end == ctx->isn_num && VAL_REG(v) >= 0){
		ctx->in_use[VAL_REG(v)] = 0;
	}
}

static void regalloc_greedy(isn *head, int nregs)
{
	struct greedy_ctx ctx = { 0 };
	int i;

	ctx.in_use = xcalloc(nregs, 1);
	ctx.nregs = nregs;

	for(; head; head = head->next, ctx.isn_num++)
		isn_on_vals(head, regalloc_greedy1, &ctx);

	for(i = 0; i < nregs; i++)
		assert(ctx.in_use[i] == 0);

	free(ctx.in_use);
}

static void simple_regalloc(isn *head, int nregs)
{
	assign_lifetimes(head);

	regalloc_greedy(head, nregs);

	free_regalloc_data(head);
}

void isn_regalloc(isn *head, int nregs)
{
	simple_regalloc(head, nregs);
}
