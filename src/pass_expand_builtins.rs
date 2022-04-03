#include <assert.h>

#include "pass_expand_builtins.h"

#include "function.h"
#include "unit.h"
#include "reg.h"
#include "isn.h"
#include "isn_struct.h"
#include "builtins.h"

struct ctx
{
	function *fn;
	unit *unit;
};

static void expand_isn(isn *isn, block *block, function *fn, unit *unit)
{
	if(isn->type == ISN_MEMCPY)
		builtin_expand_memcpy(isn, block, fn, unit);
}

static void expand_block(block *block, void *vctx)
{
	struct ctx *ctx = vctx;
	isn *i = block_first_isn(block);

	isns_flag(i, true);

	for(; i; i = isn_next(i))
		if(i->flag)
			expand_isn(i, block, ctx->fn, ctx->unit);
}

void pass_expand_builtins(function *fn, struct unit *unit, const struct target *target)
{
	struct ctx ctx;
	block *entry = function_entry_block(fn, false);
	if(!entry){
		assert(function_is_forward_decl(fn));
		return;
	}

	ctx.fn = fn;
	ctx.unit = unit;
	function_onblocks(fn, expand_block, &ctx);
}
