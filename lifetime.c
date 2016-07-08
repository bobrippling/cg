#include "lifetime.h"

#include "dynmap.h"
#include "mem.h"

#include "function.h"
#include "val.h"
#include "val_struct.h"
#include "isn.h"
#include "block_struct.h"

struct lifetime_assign_ctx
{
	block *blk;
	unsigned isn_count;
};

static void assign_lifetime(val *v, isn *isn, void *vctx)
{
	struct lifetime_assign_ctx *ctx = vctx;
	struct lifetime *lt;
	unsigned start;

	(void)isn;

	switch(v->kind){
		case FROM_ISN:
			start = ctx->isn_count;
			break;
		case ARGUMENT:
			start = 0;
			break;
		default:
			return;
	}

	lt = dynmap_get(val *, struct lifetime *, ctx->blk->val_lifetimes, v);

	if(!lt){
		lt = xcalloc(1, sizeof *lt);
		dynmap_set(val *, struct lifetime *, ctx->blk->val_lifetimes, v, lt);

		lt->start = start;
	}

	lt->end = ctx->isn_count;
}

static void lifetime_fill_block(block *b)
{
	struct lifetime_assign_ctx ctx = { 0 };
	isn *i;
	ctx.blk = b;

	for(i = block_first_isn(b); i; i = isn_next(i), ctx.isn_count++)
		isn_on_live_vals(i, assign_lifetime, &ctx);
}

void lifetime_fill_func(function *func)
{
	function_onblocks(func, lifetime_fill_block);
}
