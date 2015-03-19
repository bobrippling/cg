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
			{
				printf("\t%s %s, %s ===> %s\n",
						op_to_str(i->u.op.op),
						x86_val_str(i->u.op.lhs, 0, alloca2stack, 0),
						x86_val_str(i->u.op.rhs, 1, alloca2stack, 0),
						x86_val_str(i->u.op.res, 2, alloca2stack, 0));
				break;
			}

			case ISN_CMP:
			{
				printf("\t%s %s, %s ===> %s\n",
						op_cmp_to_str(i->u.cmp.cmp),
						x86_val_str(i->u.cmp.lhs, 0, alloca2stack, 0),
						x86_val_str(i->u.cmp.rhs, 1, alloca2stack, 0),
						x86_val_str(i->u.cmp.res, 2, alloca2stack, 0));
				break;
			}

			case ISN_COPY:
			{
				printf("\tmov %s, %s\n",
						x86_val_str(i->u.copy.from, 0, alloca2stack, 0),
						x86_val_str(i->u.copy.to, 1, alloca2stack, 0));
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
