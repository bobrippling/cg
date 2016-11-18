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

struct replace_ctx
{
	block *block;
	unsigned isn_count;
};

#define ISN_SWITCH_RW()                \
	switch(i->type){                     \
		case ISN_STORE:                    \
			RW(reads[0], i->u.store.from);   \
			RW(*write, i->u.store.lval);     \
			break;                           \
		case ISN_LOAD:                     \
			RW(reads[0], i->u.load.lval);    \
			RW(*write, i->u.load.to);        \
			break;                           \
		case ISN_ALLOCA:                   \
			RW(*write, i->u.alloca.out);     \
			break;                           \
		case ISN_ELEM:                     \
			RW(reads[0], i->u.elem.index);   \
			RW(reads[0], i->u.elem.lval);    \
			RW(*write, i->u.elem.res);       \
			break;                           \
		case ISN_PTRADD:                   \
			RW(reads[0], i->u.ptradd.lhs);   \
			RW(reads[1], i->u.ptradd.rhs);   \
			RW(*write, i->u.ptradd.out);     \
			break;                           \
		case ISN_CMP:                      \
			RW(reads[0], i->u.cmp.lhs);      \
			RW(reads[1], i->u.cmp.rhs);      \
			RW(*write, i->u.cmp.res);        \
			break;                           \
		case ISN_OP:                       \
			RW(reads[0], i->u.op.lhs);       \
			RW(reads[1], i->u.op.rhs);       \
			RW(*write, i->u.cmp.res);        \
			break;                           \
		case ISN_COPY:                     \
			RW(reads[0], i->u.copy.from);    \
			RW(*write, i->u.copy.to);        \
			break;                           \
		case ISN_EXT_TRUNC:                \
			RW(reads[0], i->u.ext.from);     \
			RW(*write, i->u.ext.to);         \
			break;                           \
		case ISN_INT2PTR:                  \
		case ISN_PTR2INT:                  \
			RW(reads[0], i->u.ptr2int.from); \
			RW(*write, i->u.ptr2int.to);     \
			break;                           \
		case ISN_PTRCAST:                  \
			RW(reads[0], i->u.ptrcast.from); \
			RW(*write, i->u.ptrcast.to);     \
			break;                           \
		case ISN_RET:                      \
			RW(reads[0], i->u.ret);          \
			break;                           \
		case ISN_JMP:                      \
			break;                           \
		case ISN_CALL:                     \
			RW(reads[0], i->u.call.fn);      \
			RW(*write, i->u.call.into);      \
			break;                           \
		case ISN_BR:                       \
			RW(reads[0], i->u.branch.cond);  \
			break;                           \
		case ISN_IMPLICIT_USE:             \
			break;                           \
	}

static void isn_vals_get(isn *i, val *reads[2], val **const write)
{
	reads[0] = NULL;
	reads[1] = NULL;
	*write = NULL;

#define RW(ar, val) ar = val
	ISN_SWITCH_RW()
#undef RW
}

static void isn_vals_set(isn *i, val *reads[2], val **const write)
{
#define RW(ar, val) if(ar && ar != val){ val_release(val); val = val_retain(ar); }
	ISN_SWITCH_RW()
#undef RW
}

static void isn_replace_read_with_load(
		isn *at_isn, val *old, val *spill, val **const read,
		const struct replace_ctx *ctx)
{
	/* FIXME: unique name */
	type *ty = val_type(old);
	val *tmp = val_new_localf(ty, "spill.tmp.%u", ctx->isn_count);
	isn *load;
	struct lifetime *lt = xmalloc(sizeof *lt);

	assert(type_eq(type_deref(val_type(spill)), ty));

	load = isn_load(tmp, spill);

	isn_insert_before(at_isn, load);

	lt->start = ctx->isn_count;
	lt->end = ctx->isn_count + 1;
	dynmap_set(
			val *, struct lifetime *,
			block_lifetime_map(ctx->block),
			tmp, lt);

	/* update the out param, which will end up replacing the value in at_isn */
	*read = tmp;
}

static void isn_replace_read_with_store(
		isn *at_isn, val *old, val *spill, val **const write,
		const struct replace_ctx *ctx)
{
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
		val *reads[2];
		val *write;
		bool writeback = false;

		isn_vals_get(any_isn, reads, &write);

		if(reads[0] == old){
			isn_replace_read_with_load(any_isn, old, spill, &reads[0], &ctx);
			writeback = true;
		}
		if(reads[1] == old){
			isn_replace_read_with_load(any_isn, old, spill, &reads[1], &ctx);
			writeback = true;
		}
		if(write == old){
			isn_replace_read_with_store(any_isn, old, spill, &write, &ctx);
			writeback = true;
		}

		if(writeback)
			isn_vals_set(any_isn, reads, &write);
	}
}

void isn_replace_val_with_val(isn *isn, val *old, val *new, enum replace_mode mode)
{
	val *reads[2], *write;

	isn_vals_get(isn, reads, &write);

	if(mode & REPLACE_READS){
		if(reads[0] == old) reads[0] = new;
		if(reads[1] == old) reads[1] = new;
	}
	if(mode & REPLACE_WRITES)
		if(write == old) write = new;

	isn_vals_set(isn, reads, &write);
}
