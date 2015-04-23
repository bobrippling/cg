#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "macros.h"

#include "x86.h"

#include "dynmap.h"
#include "isn_internal.h"
#include "isn_struct.h"
#include "val_struct.h"
#include "blk_reg.h"
#include "block_internal.h"
#include "block_struct.h"

#define x86_lbl_prefix "L_"

struct x86_alloca_ctx
{
	dynmap *alloca2stack;
	long alloca;
};

static const char *const regs[][4] = {
	{  "al", "ax", "eax", "rax" },
	{  "bl", "bx", "ebx", "rbx" },
	{  "cl", "cx", "ecx", "rcx" },
	{  "dl", "dx", "edx", "rdx" },
	{ "dil", "di", "edi", "rdi" },
	{ "sil", "si", "esi", "rsi" },
};

#define SCRATCH_REG 2 /* ecx */

typedef enum operand_category
{
	/* 0 means no entry / end of entries */
	OPERAND_REG = 1,
	OPERAND_MEM,
	OPERAND_INT
} operand_category;

struct x86_isn
{
	const char *mnemonic;
	enum {
		OPERAND_INPUT = 1 << 0,
		OPERAND_OUTPUT = 1 << 1
	} io_l, io_r;
	struct x86_isn_constraint
	{
		operand_category l, r;
	} constraints[5];
};

static const struct x86_isn isn_mov = {
	"mov",
	OPERAND_INPUT, OPERAND_INPUT | OPERAND_OUTPUT,
	{
		{ OPERAND_REG, OPERAND_REG },
		{ OPERAND_REG, OPERAND_MEM },
		{ OPERAND_MEM, OPERAND_REG },
		{ OPERAND_REG, OPERAND_INT },
		{ OPERAND_INT, OPERAND_REG }
	}
};

static const struct x86_isn isn_movzx = {
	"mov",
	OPERAND_INPUT, OPERAND_INPUT | OPERAND_OUTPUT,
	{
		{ OPERAND_REG, OPERAND_REG },
		{ OPERAND_REG, OPERAND_MEM },
	}
};

static const struct x86_isn isn_add = {
	"add",
	OPERAND_INPUT, OPERAND_INPUT | OPERAND_OUTPUT,
	{
		{ OPERAND_REG, OPERAND_REG },
		{ OPERAND_REG, OPERAND_MEM },
		{ OPERAND_MEM, OPERAND_REG },
		{ OPERAND_INT, OPERAND_REG }
	}
};

static const struct x86_isn isn_cmp = {
	"cmp",
	OPERAND_INPUT, OPERAND_INPUT,
	{
		{ OPERAND_REG, OPERAND_REG },
		{ OPERAND_REG, OPERAND_MEM },
		{ OPERAND_MEM, OPERAND_REG },
		{ OPERAND_INT, OPERAND_REG },
		{ OPERAND_INT, OPERAND_MEM },
	}
};

static const struct x86_isn isn_test = {
	"test",
	OPERAND_INPUT, OPERAND_INPUT,
	{
		{ OPERAND_REG, OPERAND_REG },
		{ OPERAND_REG, OPERAND_MEM },
		{ OPERAND_MEM, OPERAND_REG },
		{ OPERAND_INT, OPERAND_REG },
		{ OPERAND_INT, OPERAND_MEM },
	}
};


static void mov_deref(
		val *from, val *to,
		dynmap *alloca2stack,
		int dl, int dr);



static operand_category val_category(val *v)
{
	if(val_is_mem(v))
		return OPERAND_MEM;

	switch(v->type){
		case INT:  return OPERAND_INT;
		case NAME: return OPERAND_REG; /* not mem from val_is_mem() */
		default:
			assert(0 && "unreachable");
	}
	assert(0);
}

static int alloca_offset(dynmap *alloca2stack, val *val)
{
	intptr_t off = dynmap_get(struct val *, intptr_t, alloca2stack, val);
	assert(off);
	return off;
}

static const char *name_in_reg_str(val *val, /*optional*/int size)
{
	int sz_idx;
	int reg = val->u.addr.u.name.loc.u.reg;

	assert(val->u.addr.u.name.loc.where == NAME_IN_REG);

	if(reg == -1)
		return val->u.addr.u.name.spel;

	assert(reg < (int)countof(regs));

	if(size < 0)
		size = val->u.addr.u.name.val_size;

	switch(size){
		case 0:
			/* FIXME: word size */
			sz_idx = 3;
			break;

		case 1: sz_idx = 0; break;
		case 2: sz_idx = 1; break;
		case 4: sz_idx = 2; break;
		case 8: sz_idx = 3; break;
		default: assert(0);
	}

	return regs[reg][sz_idx];
}

static const char *x86_val_str_sized(
		val *val, int bufchoice,
		dynmap *alloca2stack,
		bool dereference, int size)
{
	static char bufs[3][256];

	assert(0 <= bufchoice && bufchoice <= 2);
	char *buf = bufs[bufchoice];

	switch(val->type){
		case INT:
			assert(!dereference);
			snprintf(buf, sizeof bufs[0], "$%d", val->u.i.i);
			break;
		case INT_PTR:
			assert(dereference);
			snprintf(buf, sizeof bufs[0], "%d", val->u.i.i);
			break;
		case NAME:
			if(val->u.addr.u.name.loc.where == NAME_IN_REG){
				snprintf(buf, sizeof bufs[0], "%s%%%s%s",
						dereference ? "(" : "",
						name_in_reg_str(val, dereference ? 0 : size),
						dereference ? ")" : "");
			}else{
				assert(dereference);

				snprintf(buf, sizeof bufs[0], "-%u(%%rbp)",
						val->u.addr.u.name.loc.u.off);
			}
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

static const char *x86_val_str(
		val *val, int bufchoice,
		dynmap *alloca2stack,
		bool dereference)
{
	return x86_val_str_sized(
		val, bufchoice,
		alloca2stack,
		dereference, -1);
}

static void make_val_temporary_store(
		val *from,
		val *write_to,
		operand_category from_cat,
		operand_category to_cat)
{
	/* move 'from' into category 'to_cat' (from 'from_cat'),
	 * saving the new value in *write_to */

	if(from_cat == to_cat){
		*write_to = *from;
		goto out;
	}

	assert(to_cat != OPERAND_INT);

	memset(write_to, 0, sizeof *write_to);

	if(to_cat == OPERAND_REG){
		/* use scratch register */
		write_to->type = NAME;

		write_to->u.addr.u.name.loc.where = NAME_IN_REG;
		write_to->u.addr.u.name.loc.u.reg = SCRATCH_REG;

	}else{
		assert(to_cat == OPERAND_MEM);

		write_to->type = NAME;

		write_to->u.addr.u.name.loc.where = NAME_SPILT;
		write_to->u.addr.u.name.loc.u.off = 13; /* TODO */
	}

	write_to->u.addr.u.name.val_size = val_size(from);

out:
	write_to->retains = 1;
}

static const struct x86_isn_constraint *find_isn_operand(
		const struct x86_isn *isn,
		const operand_category lhs_cat,
		const operand_category rhs_cat,
		int *const mem_reg_idx,
		int *const reg_mem_idx,
		int *const reg_reg_idx)
{
	const int max = countof(isn->constraints);
	int i;
	int satisfied;

	*mem_reg_idx = *reg_mem_idx = *reg_reg_idx = -1;

	for(i = 0; i < max && isn->constraints[i].l; i++){
		if(lhs_cat == isn->constraints[i].l
		&& rhs_cat == isn->constraints[i].r)
		{
			break;
		}

		if(isn->constraints[i].l == OPERAND_MEM
		&& isn->constraints[i].r == OPERAND_REG)
		{
			*mem_reg_idx = i;
		}

		if(isn->constraints[i].l == OPERAND_REG
		&& isn->constraints[i].r == OPERAND_MEM)
		{
			*reg_mem_idx = i;
		}

		if(isn->constraints[i].l == OPERAND_REG
		&& isn->constraints[i].r == OPERAND_REG)
		{
			*reg_reg_idx = i;
		}
	}

	satisfied = !(i == max || isn->constraints[i].l == 0);
	if(!satisfied)
		return NULL;

	return &isn->constraints[i];
}

static void ready_input(
		val *orig_val, val *temporary_store,
		operand_category orig_val_category,
		operand_category operand_category,
		int *const deref_val,
		dynmap *alloca2stack)
{
	make_val_temporary_store(
			orig_val, temporary_store,
			orig_val_category, operand_category);

	/* orig_val needs to be loaded before the instruction
	 * no dereference of temporary_store here - move into the temporary */
	mov_deref(orig_val, temporary_store, alloca2stack, *deref_val, 0);
	*deref_val = 0;
}

static void ready_output(
		val *orig_val, val *temporary_store,
		operand_category orig_val_category,
		operand_category operand_category,
		int *const deref_val)
{
	/* wait to store the value until after the main isn */
	make_val_temporary_store(
			orig_val, temporary_store,
			orig_val_category, operand_category);

	/* using a register as a temporary rhs - no dereference */
	*deref_val = 0;
}

static void emit_isn(
		const struct x86_isn *isn, dynmap *alloca2stack,
		val *const lhs, int deref_lhs,
		val *const rhs, int deref_rhs,
		const char *isn_suffix)
{
	const operand_category lhs_cat = deref_lhs ? OPERAND_MEM : val_category(lhs);
	const operand_category rhs_cat = deref_rhs ? OPERAND_MEM : val_category(rhs);
	const int orig_deref_lhs = deref_lhs;
	const int orig_deref_rhs = deref_rhs;
	int mem_reg_idx, reg_mem_idx, reg_reg_idx;
	const struct x86_isn_constraint *operands_target = NULL;
	val temporary_lhs, *emit_lhs = lhs;
	val temporary_rhs, *emit_rhs = rhs;

	if(lhs_cat == OPERAND_MEM)
		deref_lhs = 1;
	if(rhs_cat == OPERAND_MEM)
		deref_rhs = 1;

	operands_target = find_isn_operand(
			isn, lhs_cat, rhs_cat,
			&mem_reg_idx, &reg_mem_idx, &reg_reg_idx);

	if(!operands_target){
		/* not satisfied - convert an operand to REG or MEM */
		int i = -1;

		if(reg_reg_idx > -1)
			i = reg_reg_idx;
		else if(mem_reg_idx > -1)
			i = mem_reg_idx;
		else if(reg_mem_idx > -1)
			i = reg_mem_idx;

		assert(i != -1 && "unsatisfiable + unconvertable instruction operands");
		operands_target = &isn->constraints[i];

		/* ready the operands if they're inputs */
		if(isn->io_l & OPERAND_INPUT
		&& lhs_cat != operands_target->l)
		{
			ready_input(
					lhs, &temporary_lhs,
					lhs_cat, operands_target->l,
					&deref_lhs, alloca2stack);

			emit_lhs = &temporary_lhs;
		}
		if(isn->io_r & OPERAND_INPUT
		&& rhs_cat != operands_target->r)
		{
			ready_input(
					rhs, &temporary_rhs,
					rhs_cat, operands_target->r,
					&deref_rhs, alloca2stack);

			emit_rhs = &temporary_rhs;
		}

		/* ready the output operands */
		if(lhs_cat != operands_target->l
		&& isn->io_l & OPERAND_OUTPUT)
		{
			ready_output(
					lhs, &temporary_lhs,
					lhs_cat, operands_target->l,
					&deref_lhs);

			emit_lhs = &temporary_lhs;
		}
		if(rhs_cat != operands_target->r
		&& isn->io_r & OPERAND_OUTPUT)
		{
			ready_output(
					rhs, &temporary_rhs,
					rhs_cat, operands_target->r,
					&deref_rhs);

			emit_rhs = &temporary_rhs;
		}
	}else{
		/* satisfied */
	}

	printf("\t%s%s %s, %s\n",
			isn->mnemonic, isn_suffix,
			x86_val_str(emit_lhs, 0, alloca2stack, deref_lhs),
			x86_val_str(emit_rhs, 1, alloca2stack, deref_rhs));

	/* store outputs after the instruction */
	if(lhs_cat != operands_target->l
	&& isn->io_l & OPERAND_OUTPUT)
	{
		mov_deref(emit_lhs, lhs, alloca2stack, 0, orig_deref_lhs);
	}
	if(rhs_cat != operands_target->r
	&& isn->io_r & OPERAND_OUTPUT)
	{
		mov_deref(emit_rhs, rhs, alloca2stack, 0, orig_deref_rhs);
	}
}

static void mov_deref(
		val *from, val *to,
		dynmap *alloca2stack,
		int dl, int dr)
{
	if(!dl && !dr
	&& from->type == NAME
	&& to->type == NAME
	&& from->u.addr.u.name.loc.where == NAME_IN_REG
	&& to->u.addr.u.name.loc.where == NAME_IN_REG
	&& from->u.addr.u.name.loc.u.reg == to->u.addr.u.name.loc.u.reg)
	{
		printf("\t;");
	}

	emit_isn(&isn_mov, alloca2stack,
			from, dl,
			to, dr,
			"");
}

static void mov(val *from, val *to, dynmap *alloca2stack)
{
	mov_deref(from, to, alloca2stack, 0, 0);
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

		case ALLOCA:
		{
			int err;
			assert(i->u.elem.add->type == INT);

			add_total = op_exe(op_add,
					alloca_offset(alloca2stack, i->u.elem.lval),
					i->u.elem.add->u.i.i, &err);

			assert(!err);
			break;
		}
	}

	printf("\tlea %d(%%rbp), %s\n",
			add_total,
			x86_val_str_sized(i->u.elem.res, 0, alloca2stack, 0, /*ptrsize*/0));
}

static void x86_op(
		enum op op, val *lhs, val *rhs,
		val *res, dynmap *alloca2stack)
{
	/* no instruction selection / register merging. just this for now */
	mov(lhs, res, alloca2stack);

	assert(op == op_add && "TODO");

	emit_isn(
			&isn_add, /* FIXME: op_to_str / op_to_isn_xyz */
			alloca2stack,
			rhs, 0,
			res, 0,
			"");
}

static void x86_ext(val *from, val *to, dynmap *alloca2stack)
{
	unsigned sz_from = val_size(from);
	unsigned sz_to = val_size(to);
	char buf[4] = "z";

	assert(sz_to > sz_from);
	buf[3] = '\0';

	/*      1   2   4   8
	 *   1  x  zbw zbl zbq
	 *   2  x   x  zwl zwq
	 *   4  x   x   x <empty>
	 *   8  x   x   x   x
	 */

	switch(sz_from){
		case 1: buf[1] = 'b'; break;
		case 2: buf[1] = 'w'; break;
		case 4:
			assert(sz_to == 8);
			mov(from, to, alloca2stack); /* movl a, b */
			return;

		default:
			assert(0 && "bad extension");
	}

	switch(sz_to){
		case 2: buf[2] = 'w'; break;
		case 4: buf[2] = 'l'; break;
		case 8: buf[2] = 'q'; break;
		default:
			assert(0 && "bad extension");
	}

	emit_isn(&isn_movzx, alloca2stack,
			from, 0, to, 0, buf);
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

static const char *x86_size_suffix(unsigned sz)
{
	switch(sz){
		case 1: return "b";
		case 2: return "w";
		case 4: return "l";
		case 8: return "q";
	}
	assert(0);
}

static void x86_cmp(
		enum op_cmp cmp,
		val *lhs, val *rhs, val *res,
		dynmap *alloca2stack)
{
	val *zero;

	emit_isn(&isn_cmp, alloca2stack,
			lhs, 0,
			rhs, 0,
			x86_size_suffix(val_size(lhs)));

	zero = val_retain(val_new_i(0, val_size(lhs)));

	mov(zero, res, alloca2stack);

	printf("\tset%s %s\n",
			x86_cmp_str(cmp),
			x86_val_str(res, 0, alloca2stack, 0));

	val_release(zero);
}

static void x86_branch(val *cond, block *bt, block *bf, dynmap *alloca2stack)
{
	emit_isn(&isn_test, alloca2stack,
			cond, 0,
			cond, 0,
			x86_size_suffix(val_size(cond)));

	printf("\tjz  " x86_lbl_prefix "%s\n", bf->lbl);
	printf("\tjmp " x86_lbl_prefix "%s\n", bt->lbl);
}

static void x86_out_block1(block *blk, dynmap *alloca2stack)
{
	isn *head = block_first_isn(blk);
	isn *i;

	if(blk->lbl)
		printf(x86_lbl_prefix "%s:\n", blk->lbl);

	for(i = head; i; i = i->next){
		if(i->skip)
			continue;

		switch(i->type){
			case ISN_ALLOCA:
				break;

			case ISN_RET:
			{
				val veax = { 0 };

				veax.type = NAME;
				veax.u.addr.u.name.loc.where = NAME_IN_REG;
				veax.u.addr.u.name.loc.u.reg = 0; /* XXX: hard coded eax */
				veax.u.addr.u.name.val_size = val_size(i->u.ret);

				mov(i->u.ret, &veax, alloca2stack);

				printf("\tleave\n\tret\n");
				break;
			}

			case ISN_STORE:
			{
				mov_deref(i->u.store.from, i->u.store.lval, alloca2stack, 0, 1);
				break;
			}

			case ISN_LOAD:
			{
				mov_deref(i->u.load.lval, i->u.load.to, alloca2stack, 1, 0);
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

			case ISN_EXT:
				x86_ext(i->u.ext.from, i->u.ext.to, alloca2stack);
				break;

			case ISN_COPY:
			{
				mov(i->u.copy.from, i->u.copy.to, alloca2stack);
				break;
			}

			case ISN_BR:
			{
				x86_branch(
						i->u.branch.cond,
						i->u.branch.t,
						i->u.branch.f,
						alloca2stack);
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
			x86_out_block(blk->u.branch.t, alloca2stack);
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

void x86_out(function *const func)
{
	struct x86_alloca_ctx ctx = { 0 };
	ctx.alloca2stack = dynmap_new(val *, /*ref*/NULL, val_hash);
	block *entry = function_entry_block(func);

	blk_regalloc(entry, countof(regs), SCRATCH_REG);

	/* gather allocas - must be after regalloc */
	blocks_iterate(entry, x86_sum_alloca, &ctx);

	printf("%s:\n", function_name(func));

	printf("\tpush %%rbp\n\tmov %%rsp, %%rbp\n");
	printf("\tsub $%ld, %%rsp\n", ctx.alloca);

	x86_out_block(entry, ctx.alloca2stack);

	dynmap_free(ctx.alloca2stack);
}
