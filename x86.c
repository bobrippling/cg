#include <stdio.h>
#include <assert.h>
#include <stdint.h>

#include "countof.h"

#include "dynmap.h"
#include "isn_internal.h"
#include "isn_struct.h"
#include "val_struct.h"
#include "blk_reg.h"
#include "block_internal.h"
#include "block_struct.h"

struct x86_alloca_ctx
{
	dynmap *alloca2stack;
	long alloca;
};

static const char *const regs[] = {
	"eax",
	"ebx",
	"ecx",
	"edx",
	"edi",
	"esi",
};

static int alloca_offset(dynmap *alloca2stack, val *val)
{
	intptr_t off = dynmap_get(struct val *, intptr_t, alloca2stack, val);
	assert(off);
	return off;
}

static const char *name_str(val *val)
{
	if(val->u.addr.u.name.reg == -1)
		return val->u.addr.u.name.spel;

	assert(val->u.addr.u.name.reg < (int)countof(regs));
	return regs[val->u.addr.u.name.reg];
}

static const char *x86_val_str(
		val *val, int bufchoice,
		dynmap *alloca2stack,
		bool dereference)
{
	static char bufs[3][256];

	assert(0 <= bufchoice && bufchoice <= 2);
	char *buf = bufs[bufchoice];

	switch(val->type){
		case INT:
			assert(!dereference);
			snprintf(buf, sizeof bufs[0], "$%d", val->u.i);
			break;
		case INT_PTR:
			assert(dereference);
			snprintf(buf, sizeof bufs[0], "%d", val->u.i);
			break;
		case NAME:
			assert(!dereference);
			snprintf(buf, sizeof bufs[0], "%%%s", name_str(val));
			break;
		case NAME_LVAL:
			snprintf(buf, sizeof bufs[0], "%s%%%s%s",
					dereference ? "(" : "",
					name_str(val),
					dereference ? ")" : "");
			break;
		case ALLOCA:
		{
			int off = alloca_offset(alloca2stack, val);
			/*assert(!dereference);*/
			snprintf(buf, sizeof bufs[0], "%d(%%rbp)", (int)off);
			break;
		}
	}

	return buf;
}

static void x86_mov(val *from, val *to, dynmap *alloca2stack)
{
	printf("\tmov %s, %s\n",
			x86_val_str(from, 0, alloca2stack, 0),
			x86_val_str(to, 1, alloca2stack, 0));
}

static void emit_elem(isn *i, dynmap *alloca2stack)
{
	int add_total;

	switch(i->u.elem.lval->type){
		case INT:
		case NAME:
			assert(0 && "element of INT/NAME");

		case INT_PTR:
		{
			val *intptr = i->u.elem.lval;

			if(!val_op_maybe(op_add, i->u.elem.add, intptr, &add_total)){
				assert(0 && "couldn't add operands");
			}
			break;
		}

		case NAME_LVAL:
		{
			assert(0 && "TODO: add name_lval");
			assert(i->u.elem.add->type == INT);

			printf("\tlea %d(%s), %s ; NAME_LVAL\n",
					i->u.elem.add->u.i,
					x86_val_str(i->u.elem.lval, 1, alloca2stack, 0),
					x86_val_str(i->u.elem.res,  2, alloca2stack, 0));

			return;
		}

		case ALLOCA:
		{
			int err;
			assert(i->u.elem.add->type == INT);

			add_total = op_exe(op_add,
					alloca_offset(alloca2stack, i->u.elem.lval),
					i->u.elem.add->u.i, &err);

			assert(!err);
			break;
		}
	}

	printf("\tlea %d(%%rbp), %s\n",
			add_total,
			x86_val_str(i->u.elem.res, 0, alloca2stack, 0));
}

static void x86_op(
		enum op op, val *lhs, val *rhs,
		val *res, dynmap *alloca2stack)
{
	/* no instruction selection / register merging. just this for now */
	x86_mov(lhs, res, alloca2stack);

	printf("\t%s %s, %s\n",
			op_to_str(op),
			x86_val_str(rhs, 0, alloca2stack, 0),
			x86_val_str(res, 1, alloca2stack, 0));
}

static const char *x86_cmp_str(enum op_cmp cmp)
{
	switch(cmp){
		case cmp_eq: return "e";
		case cmp_ne: return "ne";
		case cmp_gt: return "gt";
		case cmp_ge: return "ge";
		case cmp_lt: return "lt";
		case cmp_le: return "le";
	}
	assert(0);
}

static void x86_cmp(
		enum op_cmp cmp,
		val *lhs, val *rhs, val *res,
		dynmap *alloca2stack)
{
	val *zero;

	printf("\tcmp %s, %s\n",
			x86_val_str(lhs, 0, alloca2stack, 0),
			x86_val_str(rhs, 1, alloca2stack, 0));

	zero = val_new_i(0);

	x86_mov(zero, res, alloca2stack);

	printf("\tset%s %s\n",
			x86_cmp_str(cmp),
			x86_val_str(res, 0, alloca2stack, 0));

	val_free(zero);
}

static void x86_out_block1(block *blk, dynmap *alloca2stack)
{
	isn *head = block_first_isn(blk);
	isn *i;

	for(i = head; i; i = i->next){
		if(i->skip)
			continue;

		switch(i->type){
			case ISN_ALLOCA:
				break;

			case ISN_RET:
			{
				/* XXX: hard coded eax */
				printf("\tmov %s, %%eax\n",
						x86_val_str(i->u.ret, 0, alloca2stack, 0));
				printf("\tleave\n\tret\n");
				break;
			}

			case ISN_STORE:
			{
				printf("\tmov %s, %s\n",
							x86_val_str(i->u.store.from, 1, alloca2stack, 0),
							x86_val_str(i->u.store.lval, 0, alloca2stack, 1));
				break;
			}

			case ISN_LOAD:
			{
				val *rval = i->u.load.lval;

				printf("\tmov %s, %s\n",
						x86_val_str(rval, 0, alloca2stack, 1),
						x86_val_str(i->u.load.to, 1, alloca2stack, 0));

				break;
			}

			case ISN_ELEM:
				emit_elem(i, alloca2stack);
				break;

			case ISN_OP:
				x86_op(i->u.op.op, i->u.op.lhs, i->u.op.rhs, i->u.op.res, alloca2stack);
				break;

			case ISN_CMP:
				x86_cmp(i->u.cmp.cmp,
						i->u.cmp.lhs, i->u.cmp.rhs, i->u.cmp.res,
						alloca2stack);
				break;

			case ISN_COPY:
			{
				x86_mov(i->u.copy.from, i->u.copy.to, alloca2stack);
				break;
			}
		}
	}
}

static void x86_out_block(block *const blk, dynmap *alloca2stack)
{
	x86_out_block1(blk, alloca2stack);
	switch(blk->type){
		case BLK_UNKNOWN:
			assert(0 && "unknown block type");
		case BLK_ENTRY:
		case BLK_EXIT:
			break;
		case BLK_BRANCH:
			printf("TODO: BRANCH B-TRUE %p\n", blk);
			x86_out_block(blk->u.branch.t, alloca2stack);
			printf("      BRANCH B-FALS %p\n", blk);
			x86_out_block(blk->u.branch.f, alloca2stack);
	}
}

static void x86_sum_alloca(block *blk, void *vctx)
{
	struct x86_alloca_ctx *const ctx = vctx;
	isn *const head = block_first_isn(blk);
	isn *i;

	for(i = head; i; i = i->next){
		if(i->skip)
			continue;

		switch(i->type){
			case ISN_ALLOCA:
			{
				ctx->alloca += i->u.alloca.sz;

				(void)dynmap_set(val *, intptr_t,
						ctx->alloca2stack,
						i->u.alloca.out, -ctx->alloca);

				break;
			}

			default:
				break;
		}
	}
}

void x86_out(block *const entry)
{
	struct x86_alloca_ctx ctx = { 0 };
	ctx.alloca2stack = dynmap_new(val *, /*ref*/NULL, val_hash);

	/* gather allocas */
	blocks_iterate(entry, x86_sum_alloca, &ctx);

	blk_regalloc(entry, countof(regs));

	printf("\tpush %%rbp\n\tmov %%rsp, %%rbp\n");
	printf("\tsub $%ld, %%rsp\n", ctx.alloca);

	x86_out_block(entry, ctx.alloca2stack);

	dynmap_free(ctx.alloca2stack);
}
