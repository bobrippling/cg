#include "isel.h"

struct isel_ctx
{
	dynarray *out;
};

static void isel_isn(isn *isn, struct isel_ctx *ctx)
{
}

static void isel_block(block *block, void *vctx)
{
	struct isel_ctx *ctx = vctx;
	isn *i = block_first_isn(block);

	isns_flag(i, true);

	for(; i; i = isn_next(i))
		if(i->flag)
			isel_isn(i, ctx);
}

void isel(dynarray *isns, function *fn, const struct target *target)
{
	struct isel_ctx ctx;
	block *entry = function_entry_block(fn, false);
	if(!entry){
		assert(function_is_forward_decl(fn));
		return;
	}

	ctx.out = isns;
	function_onblocks(fn, isel_block, &ctx);
}
