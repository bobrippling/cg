#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "mem.h"
#include "dynmap.h"

#include "regalloc.h"

#include "isn_struct.h"
#include "isn_internal.h"
#include "isn.h"
#include "val_struct.h"
#include "val_internal.h"
#include "lifetime_struct.h"
#include "function_struct.h"
#include "function.h"

#define SHOW_REGALLOC 0
#define SHOW_STACKALLOC 0

struct greedy_ctx
{
	block *blk;
	char *in_use;
	int nregs;
	unsigned isn_num;
	unsigned *spill_space;
};

struct regalloc_ctx
{
	struct regalloc_info *info;
	unsigned spill_space;
};

static void regalloc_spill(val *v, struct greedy_ctx *ctx)
{
	unsigned size;
	struct name_loc *loc;

	switch(v->kind){
		case FROM_ISN:
			size = val_size(v);
			break;
		case ALLOCA:
			size = type_size(type_deref(val_type(v)));
			break;
		default:
			assert(0 && "trying to spill val that can't be spilt");
			return;
	}

	loc = val_location(v);
	assert(loc);

	if(loc->where == NAME_SPILT)
		return; /* already done */

	loc->where = NAME_SPILT;

	assert(size && "unsized name val in regalloc");

	*ctx->spill_space += size;

	switch(v->kind){
		case FROM_ISN:
			v->u.local.loc.u.off = *ctx->spill_space;
			break;
		case ALLOCA:
			v->u.alloca.loc.u.off = *ctx->spill_space;
			break;
		default:
			assert(0 && "unreachable");
	}

	if(SHOW_STACKALLOC){
		fprintf(stderr, "stackalloc(%s, ty=%s, size=%u) => %u\n",
				val_str(v), type_to_str(val_type(v)), size, *ctx->spill_space);
	}
}

static void regalloc_mirror(val *dest, val *src)
{
	val_mirror(dest, src);
}

static void regalloc_greedy1(val *v, isn *isn, void *vctx)
{
	struct greedy_ctx *ctx = vctx;
	struct lifetime *lt;
	struct name_loc *val_locn;
	val *src, *dest;

	switch(v->kind){
		case LITERAL:
		case GLOBAL:
			/* not something we need to regalloc */
			return;

		case ARGUMENT:
			/* done elsewhere */
			return;

		case ALLOCA:
			if(isn->type == ISN_ALLOCA){
				if(SHOW_REGALLOC){
					fprintf(stderr, "regalloc(%s) => spill [because of alloca]\n",
							val_str(v));
				}

				regalloc_spill(v, ctx);
			}else{
				/* use of alloca in non-alloca isn, leave it for now */
			}
			return;

		case FROM_ISN:
			val_locn = &v->u.local.loc;
			break;

		case BACKEND_TEMP:
			return;
	}

	/* if the instruction is a no-op (e.g. ptrcast, ptr2int/int2ptr where the sizes match),
	 * then we reuse the source register/spill */
	if(isn_is_noop(isn, &src, &dest)){
		if(v == src){
			/* if we're the source register, we need allocation */
		}else{
			if(SHOW_REGALLOC){
				fprintf(stderr, "regalloc(%s) => reuse of source register\n", val_str(v));
			}

			assert(v == dest);
			regalloc_mirror(dest, src);
			isn->skip = 1;
			return;
		}
	}

	/* if it lives across blocks we must use memory */
	if(v->live_across_blocks){
		/* optimisation - ensure the value is in the same register for all blocks
		 * mem2reg or something similar should do this */
		regalloc_spill(v, ctx);
		return;
	}

	/* if already spilt, return */
	if(val_locn->where == NAME_SPILT)
		return;

	lt = dynmap_get(val *, struct lifetime *, block_lifetime_map(ctx->blk), v);
	assert(lt);

	if(lt->start == ctx->isn_num && val_locn->u.reg == -1){
		int i;

		for(i = 0; i < ctx->nregs; i++)
			if(!ctx->in_use[i])
				break;

		if(i == ctx->nregs){
			/* no reg available */
			if(SHOW_REGALLOC)
				fprintf(stderr, "regalloc(%s) => spill\n", val_str(v));

			regalloc_spill(v, ctx);

		}else{
			ctx->in_use[i] = 1;

			val_locn->where = NAME_IN_REG;
			val_locn->u.reg = i;

			if(SHOW_REGALLOC)
				fprintf(stderr, "regalloc(%s) => reg %d\n", val_str(v), i);
		}
	}

	if(!v->live_across_blocks
	&& lt->end == ctx->isn_num
	&& val_locn->where == NAME_IN_REG
	&& val_locn->u.reg >= 0)
	{
		assert(val_locn->u.reg < ctx->nregs);
		ctx->in_use[val_locn->u.reg] = 0;
	}
}

static void mark_other_block_val_as_used(val *v, isn *isn, void *vctx)
{
	char *in_use = vctx;
	int idx;

	/* isn may be null from mark_arg_vals_as_used() */
	(void)isn;

	if(val_is_mem(v))
		return;

	switch(v->kind){
		case FROM_ISN:
			assert(v->u.local.loc.where == NAME_IN_REG);
			idx = v->u.local.loc.u.reg;
			break;
		case ARGUMENT:
		{
			struct name_loc *loc = function_arg_loc(
						v->u.argument.func,
						v->u.argument.idx);

			assert(loc->where == NAME_IN_REG);

			idx = loc->u.reg;
			break;
		}
		default:
			return;
	}

	if(idx == -1)
		return;

	in_use[idx] = 1;
}

static void mark_other_block_vals_as_used(char *in_use, isn *isn)
{
	for(; isn; isn = isn->next)
		isn_on_live_vals(isn, mark_other_block_val_as_used, in_use);
}

static void mark_arg_vals_as_used(char *in_use, function *func)
{
	size_t i;

	dynarray_iter(&func->arg_locns, i){
		struct name_loc *loc = dynarray_ent(&func->arg_locns, i);

		if(loc->where == NAME_IN_REG)
			in_use[loc->u.reg] = 1;
	}
}

static void mark_callee_save_as_used(
		char *in_use, const int *callee_save, unsigned callee_save_cnt)
{
	for(; callee_save_cnt > 0; callee_save_cnt--, callee_save++)
		in_use[*callee_save] = 1;
}

static void regalloc_greedy(
		block *blk, function *func, isn *const head,
		unsigned *const spill_space,
		uniq_type_list *uniq_type_list,
		const struct backend_traits *backend)
{
	struct greedy_ctx alloc_ctx = { 0 };
	isn *isn_iter;

	alloc_ctx.in_use = xcalloc(backend->nregs, 1);
	alloc_ctx.nregs = backend->nregs;
	alloc_ctx.blk = blk;
	alloc_ctx.spill_space = spill_space;

	/* mark scratch as in use */
	alloc_ctx.in_use[backend->scratch_reg] = 1;

	/* mark values who are in use in other blocks as in use */
	mark_other_block_vals_as_used(alloc_ctx.in_use, head);

	mark_arg_vals_as_used(alloc_ctx.in_use, func);

	mark_callee_save_as_used(
			alloc_ctx.in_use,
			backend->callee_save,
			backend->callee_save_cnt);

	for(isn_iter = head; isn_iter; isn_iter = isn_iter->next, alloc_ctx.isn_num++)
		isn_on_live_vals(isn_iter, regalloc_greedy1, &alloc_ctx);

	free(alloc_ctx.in_use);

	/* any leftovers become elems in the regspill alloca block */
	if(*alloc_ctx.spill_space){
		type *spill_array_ty = type_get_ptr(
				uniq_type_list,
				type_get_array(
					uniq_type_list,
					type_get_primitive(uniq_type_list, i1),
					*alloc_ctx.spill_space));

		val *spill_array_val = val_new_temporary(spill_array_ty);

		isn_alloca(blk, spill_array_val);
	}
}

static void simple_regalloc(block *blk, struct regalloc_ctx *ctx)
{
	isn *head = block_first_isn(blk);

	regalloc_greedy(
			blk,
			ctx->info->func,
			head,
			&ctx->spill_space,
			ctx->info->uniq_type_list,
			&ctx->info->backend);
}

static void blk_regalloc_pass(block *blk, void *vctx)
{
	struct regalloc_ctx *ctx = vctx;

	simple_regalloc(blk, ctx);
}

void regalloc(block *blk, struct regalloc_info *info)
{
	struct regalloc_ctx ctx = { 0 };
	dynmap *alloc_markers = BLOCK_DYNMAP_NEW();

	ctx.info = info;

	blocks_traverse(blk, blk_regalloc_pass, &ctx, alloc_markers);

	dynmap_free(alloc_markers);
}
