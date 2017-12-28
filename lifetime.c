#include <assert.h>

#include "lifetime.h"

#include "dynmap.h"
#include "mem.h"

#include "function.h"
#include "function_struct.h"
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

	(void)isn;

	switch(v->kind){
		case ABI_TEMP:
		case FROM_ISN:
		case BACKEND_TEMP:
		case ARGUMENT:
			/* even though arguments technically exist
			 * from the start, we count their lifetime
			 * from their first use,
			 * i.e. pass_abi's first assignment from
			 * an ABI register
			 */
		case ALLOCA:
			/* alloc these so regalloc can find them in
			 * the lifetime map to assign spill locations */
			break;
		case LITERAL:
		case GLOBAL:
		case LABEL:
		case UNDEF:
			return;
	}

	lt = dynmap_get(val *, struct lifetime *, ctx->blk->val_lifetimes, v);

	if(!lt){
		lt = xcalloc(1, sizeof *lt);
		dynmap_set(val *, struct lifetime *, ctx->blk->val_lifetimes, v, lt);

		lt->start = isn;
	}

	lt->end = isn;
}

bool lifetime_contains(const struct lifetime *lt, isn *needle, bool include_last)
{
	isn *i;
	for(i = lt->start; i != lt->end; i = isn_next(i))
		if(i == needle)
			return true;

	if(include_last && i == lt->end && lt->end)
		return true;

	return false;
}

static void lifetime_fill_block(block *b, void *vctx)
{
	struct lifetime_assign_ctx ctx = { 0 };
	isn *i;
	ctx.blk = b;

	(void)vctx;

	for(i = block_first_isn(b); i; i = isn_next(i), ctx.isn_count++)
		isn_on_live_vals(i, assign_lifetime, &ctx);
}

void lifetime_fill_func(function *func)
{
	assert(!func->lifetime_filled);
	function_onblocks(func, lifetime_fill_block, NULL);
	func->lifetime_filled = true;
}
