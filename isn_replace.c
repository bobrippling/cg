#include <assert.h>

#include "mem.h"
#include "dynmap.h"

#include "isn_replace.h"

#include "isn.h"
#include "isn_struct.h"
#include "val.h"
#include "val_internal.h"
#include "type.h"
#include "lifetime_struct.h"
#include "location.h"

struct replace_ctx
{
	block *block;
	unsigned isn_count;
};

#define ISN_SWITCH_IO()                \
	switch(i->type){                     \
		case ISN_STORE:                    \
			IO(inputs[0], i->u.store.from);   \
			IO(inputs[1], i->u.store.lval);     \
			break;                           \
		case ISN_LOAD:                     \
			IO(inputs[0], i->u.load.lval);    \
			IO(*output, i->u.load.to);        \
			break;                           \
		case ISN_ALLOCA:                   \
			IO(*output, i->u.alloca.out);     \
			break;                           \
		case ISN_ELEM:                     \
			IO(inputs[0], i->u.elem.index);   \
			IO(inputs[1], i->u.elem.lval);    \
			IO(*output, i->u.elem.res);       \
			break;                           \
		case ISN_PTRADD:                   \
		case ISN_PTRSUB:                   \
			IO(inputs[0], i->u.ptraddsub.lhs);\
			IO(inputs[1], i->u.ptraddsub.rhs);\
			IO(*output, i->u.ptraddsub.out);  \
			break;                           \
		case ISN_CMP:                      \
			IO(inputs[0], i->u.cmp.lhs);      \
			IO(inputs[1], i->u.cmp.rhs);      \
			IO(*output, i->u.cmp.res);        \
			break;                           \
		case ISN_OP:                       \
			IO(inputs[0], i->u.op.lhs);       \
			IO(inputs[1], i->u.op.rhs);       \
			IO(*output, i->u.cmp.res);        \
			break;                           \
		case ISN_COPY:                     \
			IO(inputs[0], i->u.copy.from);    \
			IO(*output, i->u.copy.to);        \
			break;                           \
		case ISN_EXT_TRUNC:                \
			IO(inputs[0], i->u.ext.from);     \
			IO(*output, i->u.ext.to);         \
			break;                           \
		case ISN_INT2PTR:                  \
		case ISN_PTR2INT:                  \
			IO(inputs[0], i->u.ptr2int.from); \
			IO(*output, i->u.ptr2int.to);     \
			break;                           \
		case ISN_PTRCAST:                  \
			IO(inputs[0], i->u.ptrcast.from); \
			IO(*output, i->u.ptrcast.to);     \
			break;                           \
		case ISN_RET:                      \
			IO(inputs[0], i->u.ret);          \
			break;                           \
		case ISN_JMP:                      \
			break;                           \
		case ISN_CALL:                     \
			IO(inputs[0], i->u.call.fn);      \
			IO(*output, i->u.call.into);      \
			break;                           \
		case ISN_BR:                       \
			IO(inputs[0], i->u.branch.cond);  \
			break;                           \
		case ISN_IMPLICIT_USE:             \
			break;                           \
	}

void isn_vals_get(isn *i, val *inputs[2], val **const output)
{
	inputs[0] = NULL;
	inputs[1] = NULL;
	*output = NULL;

#define IO(ar, val) ar = val
	ISN_SWITCH_IO()
#undef IO
}

static void isn_vals_set(isn *i, val *inputs[2], val **const output)
{
#define IO(ar, val) if(ar && ar != val){ val_release(val); val = val_retain(ar); }
	ISN_SWITCH_IO()
#undef IO
}

static void isn_replace_input_with_load(
		isn *at_isn, val *old, val *spill, val **const input,
		const struct replace_ctx *ctx)
{
	/* FIXME: unique name */
	type *ty = val_type(old);
	val *tmp = val_new_localf(ty, "reload.%u", ctx->isn_count);
	isn *load;
	struct lifetime *lt = xmalloc(sizeof *lt);

	assert(type_eq(type_deref(val_type(spill)), ty));

	load = isn_load(tmp, spill);

	isn_insert_before(at_isn, load);

	lt->start = load;
	lt->end = at_isn;
	dynmap_set(
			val *, struct lifetime *,
			block_lifetime_map(ctx->block),
			tmp, lt);

	/* update the out param, which will end up replacing the value in at_isn */
	*input = tmp;
}

static void isn_replace_output_with_store(
		isn *at_isn, val *old, val *spill, val **const output,
		const struct replace_ctx *ctx)
{
	/* FIXME: unique name */
	type *ty = val_type(old);
	val *tmp = val_new_localf(ty, "reload.%u", ctx->isn_count);
	isn *store;
	struct lifetime *lt = xmalloc(sizeof *lt);

	assert(type_eq(type_deref(val_type(spill)), ty));

	store = isn_store(tmp, spill);

	isn_insert_after(at_isn, store);

	lt->start = at_isn;
	lt->end = store;
	dynmap_set(
			val *, struct lifetime *,
			block_lifetime_map(ctx->block),
			tmp, lt);

	/* update the out param, which will end up replacing the value in at_isn */
	*output = tmp;
}

void isn_replace_uses_with_load_store(
		struct val *old, struct val *spill, struct isn *any_isn, block *blk)
{
	struct replace_ctx ctx = { 0 };

	ctx.block = blk;
	ctx.isn_count = 0;

	for(any_isn = isn_first(any_isn);
			any_isn;
			any_isn = isn_next(any_isn), ctx.isn_count++)
	{
		val *inputs[2];
		val *output;
		bool writeback = false;

		isn_vals_get(any_isn, inputs, &output);

		if(inputs[0] == old){
			isn_replace_input_with_load(any_isn, old, spill, &inputs[0], &ctx);
			writeback = true;
		}
		if(inputs[1] == old){
			isn_replace_input_with_load(any_isn, old, spill, &inputs[1], &ctx);
			writeback = true;
		}
		if(output == old){
			isn_replace_output_with_store(any_isn, old, spill, &output, &ctx);
			writeback = true;
		}

		if(writeback)
			isn_vals_set(any_isn, inputs, &output);
	}
}

void isn_replace_val_with_val(isn *isn, val *old, val *new, enum replace_mode mode)
{
	val *inputs[2], *output;

	isn_vals_get(isn, inputs, &output);

	if(mode & REPLACE_INPUTS){
		if(inputs[0] == old) inputs[0] = new;
		if(inputs[1] == old) inputs[1] = new;
	}
	if(mode & REPLACE_OUTPUTS)
		if(output == old) output = new;

	isn_vals_set(isn, inputs, &output);
}
