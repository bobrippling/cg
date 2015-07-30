#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "macros.h"
#include "die.h"
#include "io.h"
#include "mem.h"
#include "dynmap.h"

#include "x86.h"

#include "isn_internal.h"
#include "isn_struct.h"
#include "val_struct.h"
#include "regalloc_blk.h"
#include "block_struct.h"
#include "function_struct.h"
#include "variable_struct.h"
#include "unit.h"
#include "val_internal.h" /* val_location() */
#include "global_struct.h"

struct x86_alloca_ctx
{
	dynmap *alloca2stack;
	long alloca;
};

typedef struct x86_out_ctx
{
	dynmap *alloca2stack;
	block *exitblk;
	function *func;
	unit *unit;
	FILE *fout;
	long alloca_bottom; /* max of ALLOCA instructions */
	long spill_alloca_max; /* max of spill space */
	unsigned max_align;
} x86_octx;

struct x86_spill_ctx
{
	dynmap *alloca2stack;
	dynmap *dontspill;
	dynmap *spill;
	block *blk;
	unsigned call_isn_idx;
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
#define PTR_SZ 8

static const int callee_saves[] = {
	1 /* ebx */
};

static const int arg_regs[] = {
	4,
	5,
	3,
	2,
	/* TODO: r8, r9 */
};

enum deref_type
{
	DEREFERENCE_FALSE, /* match 0 for false */
	DEREFERENCE_TRUE,  /* match 1 for true */
	DEREFERENCE_ANY    /* for debugging - don't crash */
};

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
		x86_octx *,
		int dl, int dr);



static operand_category val_category(val *v)
{
	if(val_is_mem(v))
		return OPERAND_MEM;

	switch(v->kind){
		case LITERAL:
			return OPERAND_INT;

		case GLOBAL:
			assert(0);

		case BACKEND_TEMP:
		case ARGUMENT:
		case FROM_ISN:
			/* not mem from val_is_mem() */
			return OPERAND_REG;
	}
	assert(0);
}

#if 0
static int alloca_offset(dynmap *alloca2stack, val *val)
{
	intptr_t off = dynmap_get(struct val *, intptr_t, alloca2stack, val);
	assert(off);
	return off;
}
#endif

static const char *name_in_reg_str(
		const struct name_loc *loc, /*optional*/int size, val *v)
{
	int sz_idx;
	int reg = loc->u.reg;

	assert(loc->where == NAME_IN_REG);

	if(reg == -1)
		return NULL;

	assert(reg < (int)countof(regs));

	if(size < 0)
		size = val_size(v);

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

static void assert_deref(enum deref_type got, enum deref_type expected)
{
	if(got == DEREFERENCE_ANY)
		return;
	assert(got == expected && "wrong deref");
}

static const char *x86_name_str(
		const struct name_loc *loc,
		char *buf, size_t bufsz,
		enum deref_type dereference_ty, type *ty,
		val *val)
{
	switch(loc->where){
		case NAME_IN_REG:
		{
			const bool deref = (dereference_ty == DEREFERENCE_TRUE);

			snprintf(buf, bufsz, "%s%%%s%s",
					deref ? "(" : "",
					name_in_reg_str(loc, deref ? 0 : type_size(ty), val),
					deref ? ")" : "");
			break;
		}

		case NAME_SPILT:
			assert_deref(dereference_ty, DEREFERENCE_TRUE);

			snprintf(buf, bufsz, "-%u(%%rbp)", loc->u.off);
			break;
	}

	return buf;
}

static const char *x86_val_str_sized(
		val *val, int bufchoice,
		x86_octx *octx,
		enum deref_type dereference)
{
	static char bufs[3][256];

	assert(0 <= bufchoice && bufchoice <= 2);
	char *buf = bufs[bufchoice];

	(void)octx;

	switch(val->kind){
		struct name_loc *loc;

		case LITERAL:
		{
			bool indir = (dereference == DEREFERENCE_TRUE);

			snprintf(buf, sizeof bufs[0],
					"%s%d",
					indir ? "" : "$",
					val->u.i);
			break;
		}

		case GLOBAL:
		{
			bool indir = (dereference == DEREFERENCE_TRUE);

			snprintf(buf, sizeof bufs[0],
					"%s%s(%%rip)",
					indir ? "" : "$",
					global_name(val->u.global));
			break;
		}

		case ARGUMENT:
			loc = function_arg_loc(val->u.argument.func, val->u.argument.idx);
			goto loc;

		case FROM_ISN: loc = &val->u.local.loc; goto loc;
		case BACKEND_TEMP: loc = &val->u.temp_loc; goto loc;
loc:
			return x86_name_str(
					loc,
					buf, sizeof bufs[0],
					dereference,
					val->ty,
					val);
	}

	return buf;
}

static const char *x86_val_str(
		val *val, int bufchoice,
		x86_octx *octx,
		enum deref_type dereference)
{
	return x86_val_str_sized(val, bufchoice, octx, dereference);
}

static void temporary_init(val *tmp, type *ty)
{
	assert(ty);

	memset(tmp, 0, sizeof *tmp);

	tmp->kind = BACKEND_TEMP;
	tmp->ty = ty;
	tmp->retains = 1;
}

static void make_stack_slot(val *stack_slot, unsigned off, type *ty)
{
	temporary_init(stack_slot, ty);

	stack_slot->u.temp_loc.where = NAME_SPILT;
	stack_slot->u.temp_loc.u.off = off;
}

attr_nonnull()
static void make_reg(val *reg, int regidx, type *ty)
{
	temporary_init(reg, ty);

	reg->u.temp_loc.where = NAME_IN_REG;
	reg->u.temp_loc.u.reg = regidx;
}

static void temporary_x86_make_eax(val *out, type *ty)
{
	make_reg(out, /* XXX: hard coded eax: */ 0, ty);
}

static void make_val_temporary_store(
		val *from,
		val *write_to,
		operand_category from_cat,
		operand_category to_cat,
		val *other_val)
{
	/* move 'from' into category 'to_cat' (from 'from_cat'),
	 * saving the new value in *write_to */

	if(from_cat == to_cat){
		*write_to = *from;
		return;
	}

	assert(to_cat != OPERAND_INT);

	if(to_cat == OPERAND_REG){
		/* use scratch register */
		temporary_init(write_to, from->ty);

		write_to->u.local.loc.where = NAME_IN_REG;
		write_to->u.local.loc.u.reg = SCRATCH_REG;

	}else{
		assert(to_cat == OPERAND_MEM);

		temporary_init(write_to, from->ty);

		write_to->u.local.loc.where = NAME_SPILT;
		write_to->u.local.loc.u.off = 133; /* TODO */

		fprintf(stderr, "WARNING: to memory temporary - incomplete\n");
	}

	assert(val_size(from) == val_size(other_val));
}

static bool operand_type_convertible(
		operand_category from, operand_category to)
{
	if(to == OPERAND_INT)
		return from == OPERAND_INT;

	return true;
}

static const struct x86_isn_constraint *find_isn_bestmatch(
		const struct x86_isn *isn,
		const operand_category lhs_cat,
		const operand_category rhs_cat,
		bool *const is_exactmatch)
{
	const int max = countof(isn->constraints);
	int i, bestmatch_i = -1;

	for(i = 0; i < max && isn->constraints[i].l; i++){
		bool match_l = (lhs_cat == isn->constraints[i].l);
		bool match_r = (rhs_cat == isn->constraints[i].r);

		if(match_l && match_r){
			*is_exactmatch = true;
			return &isn->constraints[i];
		}

		if(bestmatch_i != -1)
			continue;

		/* we can only have the best match if the non-matched operand
		 * is convertible to the required operand type */
		if((match_l && operand_type_convertible(rhs_cat, isn->constraints[i].r))
		|| (match_r && operand_type_convertible(lhs_cat, isn->constraints[i].l)))
		{
			bestmatch_i = i;
		}
	}

	*is_exactmatch = false;

	if(bestmatch_i != -1)
		return &isn->constraints[bestmatch_i];

	return NULL;
}

static void ready_input(
		val *orig_val,
		val *temporary_store,
		operand_category orig_val_category,
		operand_category operand_category,
		int *const deref_val,
		val *other_val,
		x86_octx *octx)
{
	make_val_temporary_store(
			orig_val, temporary_store,
			orig_val_category, operand_category,
			other_val);

	/* orig_val needs to be loaded before the instruction
	 * no dereference of temporary_store here - move into the temporary */
	mov_deref(orig_val, temporary_store, octx, *deref_val, 0);
	*deref_val = 0;
}

static void ready_output(
		val *orig_val,
		val *temporary_store,
		operand_category orig_val_category,
		operand_category operand_category,
		int *const deref_val,
		val *other_val)
{
	/* wait to store the value until after the main isn */
	make_val_temporary_store(
			orig_val, temporary_store,
			orig_val_category, operand_category,
			other_val);

	/* using a register as a temporary rhs - no dereference */
	*deref_val = 0;
}

static void emit_isn(
		const struct x86_isn *isn, x86_octx *octx,
		val *const lhs, int deref_lhs,
		val *const rhs, int deref_rhs,
		const char *isn_suffix)
{
	const operand_category lhs_cat = deref_lhs ? OPERAND_MEM : val_category(lhs);
	const operand_category rhs_cat = deref_rhs ? OPERAND_MEM : val_category(rhs);
	const int orig_deref_lhs = deref_lhs;
	const int orig_deref_rhs = deref_rhs;
	bool is_exactmatch;
	const struct x86_isn_constraint *operands_target = NULL;
	val *emit_lhs = lhs;
	val *emit_rhs = rhs;
	struct
	{
		val val;
		variable var;
	} temporary_lhs, temporary_rhs;

	if(lhs_cat == OPERAND_MEM)
		deref_lhs = 1;
	if(rhs_cat == OPERAND_MEM)
		deref_rhs = 1;

	operands_target = find_isn_bestmatch(isn, lhs_cat, rhs_cat, &is_exactmatch);

	assert(operands_target && "couldn't satisfy operands for isn");

	if(!is_exactmatch){
		/* not satisfied - convert an operand to REG or MEM */

		/* ready the operands if they're inputs */
		if(isn->io_l & OPERAND_INPUT
		&& lhs_cat != operands_target->l)
		{
			ready_input(
					lhs, &temporary_lhs.val,
					lhs_cat, operands_target->l,
					&deref_lhs, rhs, octx);

			emit_lhs = &temporary_lhs.val;
		}
		if(isn->io_r & OPERAND_INPUT
		&& rhs_cat != operands_target->r)
		{
			ready_input(
					rhs, &temporary_rhs.val,
					rhs_cat, operands_target->r,
					&deref_rhs, lhs, octx);

			emit_rhs = &temporary_rhs.val;
		}

		/* ready the output operands */
		if(lhs_cat != operands_target->l
		&& isn->io_l & OPERAND_OUTPUT)
		{
			ready_output(
					lhs, &temporary_lhs.val,
					lhs_cat, operands_target->l,
					&deref_lhs,
					rhs);

			emit_lhs = &temporary_lhs.val;
		}
		if(rhs_cat != operands_target->r
		&& isn->io_r & OPERAND_OUTPUT)
		{
			ready_output(
					rhs, &temporary_rhs.val,
					rhs_cat, operands_target->r,
					&deref_rhs,
					lhs);

			emit_rhs = &temporary_rhs.val;
		}
	}else{
		/* satisfied */
	}

	fprintf(octx->fout, "\t%s%s %s, %s\n",
			isn->mnemonic, isn_suffix,
			x86_val_str(emit_lhs, 0, octx, deref_lhs),
			x86_val_str(emit_rhs, 1, octx, deref_rhs));

	/* store outputs after the instruction */
	if(lhs_cat != operands_target->l
	&& isn->io_l & OPERAND_OUTPUT)
	{
		mov_deref(emit_lhs, lhs, octx, 0, orig_deref_lhs);
	}
	if(rhs_cat != operands_target->r
	&& isn->io_r & OPERAND_OUTPUT)
	{
		mov_deref(emit_rhs, rhs, octx, 0, orig_deref_rhs);
	}
}

static void mov_deref(
		val *from, val *to,
		x86_octx *octx,
		int dl, int dr)
{
	if(!dl && !dr){
		struct name_loc *loc_from, *loc_to;

		loc_from = val_location(from);
		loc_to = val_location(to);

		if(loc_from && loc_to
		&& loc_from->where == NAME_IN_REG
		&& loc_to->where == NAME_IN_REG
		&& loc_from->u.reg == loc_to->u.reg)
		{
			fprintf(octx->fout, "\t;");
		}
	}

	emit_isn(&isn_mov, octx,
			from, dl,
			to, dr,
			"");
}

static void mov(val *from, val *to, x86_octx *octx)
{
	mov_deref(from, to, octx, 0, 0);
}

#if 0
static void emit_elem(isn *i, x86_octx *octx)
{
	int add_total;

	switch(i->u.elem.lval->kind){
		case INT:
		case NAME:
		case ARG:
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
					alloca_offset(octx->alloca2stack, i->u.elem.lval),
					i->u.elem.add->u.i.i, &err);

			assert(!err);
			break;
		}

		case LBL:
		{
			int err;
			assert(i->u.elem.add->type == INT);

			add_total = op_exe(
					op_add,
					i->u.elem.lval->u.addr.u.lbl.offset,
					i->u.elem.add->u.i.i,
					&err);

			assert(!err);

			fprintf(octx->fout, "\tlea %s+%u(%%rip), %s\n",
					i->u.elem.lval->u.addr.u.lbl.spel,
					add_total,
					x86_val_str_sized(i->u.elem.res, 0, octx, 0, /*ptrsize*/0));

			return;
		}

	}

	fprintf(octx->fout, "\tlea %d(%%rbp), %s\n",
			add_total,
			x86_val_str_sized(i->u.elem.res, 0, octx, 0, /*ptrsize*/0));
}
#endif

static void x86_op(
		enum op op, val *lhs, val *rhs,
		val *res, x86_octx *octx)
{
	struct x86_isn opisn;

	/* no instruction selection / register merging. just this for now */
	mov(lhs, res, octx);

	opisn = isn_add;

	switch(op){
		case op_add:
			break;
		case op_sub:
			opisn.mnemonic = "sub";
			break;
		case op_mul:
			opisn.mnemonic = "imul";
			break;
		default:
			assert(0 && "TODO: other ops");
	}

	emit_isn(&opisn, octx, rhs, 0, res, 0, "");
}

static void x86_ext(val *from, val *to, x86_octx *octx)
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
		{
			val to_shrunk;

			to_shrunk = *to;

			to_shrunk.ty = type_get_primitive(unit_uniqtypes(octx->unit), i4);

			assert(sz_to == 8);
			fprintf(octx->fout, "\t# zext:\n");
			mov(from, &to_shrunk, octx); /* movl a, b */
			return;
		}

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

	emit_isn(&isn_movzx, octx,
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
		x86_octx *octx)
{
	val *zero;

	emit_isn(&isn_cmp, octx,
			lhs, 0,
			rhs, 0,
			x86_size_suffix(val_size(lhs)));

	zero = val_retain(val_new_i(0, lhs->ty));

	mov(zero, res, octx);

	fprintf(octx->fout, "\tset%s %s\n",
			x86_cmp_str(cmp),
			x86_val_str(res, 0, octx, 0));

	val_release(zero);
}

static void x86_jmp(x86_octx *octx, block *target)
{
	fprintf(octx->fout, "\tjmp %s\n", target->lbl);
}

static void x86_branch(val *cond, block *bt, block *bf, x86_octx *octx)
{
	emit_isn(&isn_test, octx,
			cond, 0,
			cond, 0,
			x86_size_suffix(val_size(cond)));

	fprintf(octx->fout, "\tjz %s\n", bf->lbl);
	x86_jmp(octx, bt);
}

static void x86_block_enter(x86_octx *octx, block *blk)
{
	(void)octx;
	if(blk->lbl)
		fprintf(octx->fout, "%s:\n", blk->lbl);
}

static void maybe_spill(val *v, isn *isn, void *vctx)
{
	const struct x86_spill_ctx *ctx = vctx;
	struct lifetime *lt;
	struct lifetime lt_inf = LIFETIME_INIT_INF;

	(void)isn;

	switch(v->kind){
		case LITERAL:
		case GLOBAL:
			return; /* no need to spill */

		case BACKEND_TEMP:
		case FROM_ISN:
		case ARGUMENT:
			break;
	}

	if(dynmap_exists(val *, ctx->dontspill, v))
		return;

	lt = dynmap_get(val *, struct lifetime *, ctx->blk->val_lifetimes, v);
	if(!lt)
		lt = &lt_inf;

	/* don't spill if the value ends on the isn */
	if(lt->start <= ctx->call_isn_idx
	&& lt->end > ctx->call_isn_idx)
	{
		dynmap_set(val *, void *, ctx->spill, v, (void *)NULL);
	}
}

static dynmap *x86_spillregs(
		block *blk,
		val *except[],
		unsigned call_isn_idx,
		x86_octx *octx)
{
	struct x86_spill_ctx spillctx;
	isn *isn;
	val **vi;
	size_t idx;
	val *v;
	long spill_alloca = 0;

	spillctx.spill     = dynmap_new(val *, NULL, val_hash);
	spillctx.dontspill = dynmap_new(val *, NULL, val_hash);
	spillctx.alloca2stack = octx->alloca2stack;
	spillctx.call_isn_idx = call_isn_idx;
	spillctx.blk = blk;

	for(vi = except; *vi; vi++){
		dynmap_set(val *, void *, spillctx.dontspill, *vi, (void *)NULL);
	}

	for(isn = block_first_isn(blk); isn; isn = isn->next){
		if(isn->skip)
			continue;

		isn_on_live_vals(isn, maybe_spill, &spillctx);
	}

	/* can't just spill regs in this block, need to spill 'live' regs,
	 * e.g.  argument regs */
	function_arg_vals(octx->func, &arg_vals);
	dynarray_iter(&arg_vals, idx){
		maybe_spill(dynarray_ent(&arg_vals, idx), NULL, &spillctx);
	}
	dynarray_reset(&arg_vals);

	for(idx = 0; (v = dynmap_key(val *, spillctx.spill, idx)); idx++){
		val stack_slot = { 0 };

		spill_alloca += type_size(v->ty);

		make_stack_slot(&stack_slot, octx->alloca_bottom + spill_alloca, v->ty);

		fprintf(octx->fout, "\t# spill '%s'\n", val_str(v));

		mov_deref(v, &stack_slot, octx, 0, 1);

		assert(stack_slot.kind == BACKEND_TEMP);
		assert(stack_slot.u.temp_loc.where == NAME_SPILT);
		dynmap_set(
				val *, uintptr_t,
				spillctx.spill,
				v,
				(uintptr_t)stack_slot.u.temp_loc.u.off);
	}

	if(spill_alloca > octx->spill_alloca_max)
		octx->spill_alloca_max = spill_alloca;

	dynmap_free(spillctx.dontspill);
	return spillctx.spill;
}

static void x86_restoreregs(dynmap *regs, x86_octx *octx)
{
	size_t idx;
	val *v;

	for(idx = 0; (v = dynmap_key(val *, regs, idx)); idx++){
		unsigned off = dynmap_value(uintptr_t, regs, idx);
		val stack_slot = { 0 };

		fprintf(octx->fout, "\t# restore '%s'\n", val_str(v));

		make_stack_slot(&stack_slot, off, v->ty);

		mov_deref(&stack_slot, v, octx, 1, 0);
	}

	dynmap_free(regs);
}

static void x86_call(
		block *blk, unsigned isn_idx,
		val *into_or_null, val *fn,
		dynarray *args,
		x86_octx *octx)
{
	val *except[3];
	dynmap *spilt;
	size_t i;

	except[0] = fn;
	except[1] = into_or_null;
	except[2] = NULL;

	octx->max_align = 16; /* ensure 16-byte alignment for calls */

	spilt = x86_spillregs(blk, except, isn_idx, octx);

	/* all regs spilt, can now shift arguments into arg regs */
	dynarray_iter(args, i){
		val *arg = dynarray_ent(args, i);

		if(i < countof(arg_regs)){
			val reg;

			make_reg(&reg, arg_regs[i], arg->ty);

			mov(arg, &reg, octx);

		}else{
			assert(0 && "TODO: stack args");
		}
	}

	if(fn->kind == GLOBAL){
		fprintf(octx->fout, "\tcall %s\n", global_name(fn->u.global));
	}else{
		/* TODO: isn */
		fprintf(octx->fout, "\tcall *%s\n", x86_val_str(fn, 0, octx, 0));
	}

	if(into_or_null){
		type *ty = type_func_call(type_deref(fn->ty), NULL);
		val eax;

		temporary_x86_make_eax(&eax, ty);

		mov(&eax, into_or_null, octx);
	}

	x86_restoreregs(spilt, octx);
}

static void x86_out_block1(x86_octx *octx, block *blk)
{
	isn *head = block_first_isn(blk);
	isn *i;
	unsigned idx;

	x86_block_enter(octx, blk);

	for(i = head, idx = 0; i; i = i->next, idx++){
		if(i->skip)
			continue;

		switch(i->type){
			case ISN_ALLOCA:
				break;

			case ISN_RET:
			{
				if(!type_is_void(i->u.ret->ty)){
					val veax;
					temporary_x86_make_eax(&veax, i->u.ret->ty);

					mov(i->u.ret, &veax, octx);
				}

				fprintf(octx->fout, "\tjmp %s\n", octx->exitblk->lbl);
				break;
			}

			case ISN_STORE:
			{
				mov_deref(i->u.store.from, i->u.store.lval, octx, 0, 1);
				break;
			}

			case ISN_LOAD:
			{
				mov_deref(i->u.load.lval, i->u.load.to, octx, 1, 0);
				break;
			}

			case ISN_ELEM:
				/*emit_elem(i, octx);*/
				fprintf(stderr, "ELEM UNSUPPORTED\n");
				break;

			case ISN_OP:
				x86_op(i->u.op.op, i->u.op.lhs, i->u.op.rhs, i->u.op.res, octx);
				break;

			case ISN_CMP:
				x86_cmp(i->u.cmp.cmp,
						i->u.cmp.lhs, i->u.cmp.rhs, i->u.cmp.res,
						octx);
				break;

			case ISN_EXT:
				x86_ext(i->u.ext.from, i->u.ext.to, octx);
				break;

			case ISN_COPY:
			{
				mov(i->u.copy.from, i->u.copy.to, octx);
				break;
			}

			case ISN_JMP:
			{
				x86_jmp(octx, i->u.jmp.target);
				break;
			}

			case ISN_BR:
			{
				x86_branch(
						i->u.branch.cond,
						i->u.branch.t,
						i->u.branch.f,
						octx);
				break;
			}

			case ISN_CALL:
			{
				x86_call(blk, idx,
						i->u.call.into_or_null,
						i->u.call.fn,
						&i->u.call.args,
						octx);
				break;
			}
		}
	}
}

static void x86_out_block(block *const blk, x86_octx *octx)
{
	bool *const flag = block_flag(blk);

	if(*flag)
		return;
	*flag = true;

	x86_out_block1(octx, blk);
	switch(blk->type){
		case BLK_UNKNOWN:
			assert(0 && "unknown block type");
		case BLK_ENTRY:
		case BLK_EXIT:
			break;
		case BLK_BRANCH:
			x86_out_block(blk->u.branch.t, octx);
			x86_out_block(blk->u.branch.f, octx);
			break;
		case BLK_JMP:
			x86_out_block(blk->u.jmp.target, octx);
			break;
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
				type *sz_ty = type_deref(i->u.alloca.out->ty);

				ctx->alloca += type_size(sz_ty);

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

static void x86_emit_epilogue(x86_octx *octx, block *exit)
{
	x86_block_enter(octx, exit);
	fprintf(octx->fout, "\tleave\n" "\tret\n");
}

static void x86_emit_prologue(function *func, long alloca_total, unsigned align)
{
	const char *fname;

	printf(".text\n");
	fname = function_name(func);
	printf(".globl %s\n", fname);
	printf("%s:\n", fname);

	printf("\tpush %%rbp\n"
			"\tmov %%rsp, %%rbp\n");

	if(align){
		alloca_total = (alloca_total + align) & ~(align - 1);
	}

	if(alloca_total)
		printf("\tsub $%ld, %%rsp\n", alloca_total);
}

static void x86_init_regalloc_context(
		struct regalloc_context *ctx,
		function *func)
{
	ctx->backend.nregs = countof(regs);
	ctx->backend.scratch_reg = SCRATCH_REG;
	ctx->backend.ptrsz = PTR_SZ;
	ctx->backend.callee_save = callee_saves;
	ctx->backend.callee_save_cnt = countof(callee_saves);
	ctx->backend.arg_regs = arg_regs;
	ctx->backend.arg_regs_cnt = countof(arg_regs);
	ctx->func = func;
}

static void x86_out_fn(unit *unit, function *func)
{
	struct x86_alloca_ctx alloca_ctx = { 0 };
	struct x86_out_ctx out_ctx = { 0 };
	block *const entry = function_entry_block(func, false);
	block *const exit = function_exit_block(func);
	struct regalloc_context regalloc;

	out_ctx.unit = unit;

	out_ctx.fout = tmpfile();
	if(!out_ctx.fout)
		die("tmpfile():");

	alloca_ctx.alloca2stack = dynmap_new(val *, /*ref*/NULL, val_hash);

	/* regalloc */
	x86_init_regalloc_context(&regalloc, func);
	func_regalloc(func, &regalloc);

	/* gather allocas - must be after regalloc */
	blocks_traverse(entry, x86_sum_alloca, &alloca_ctx);

	out_ctx.alloca2stack = alloca_ctx.alloca2stack;
	out_ctx.exitblk = exit;
	out_ctx.func = func;

	/* start at the bottom of allocas */
	out_ctx.alloca_bottom = alloca_ctx.alloca;

	x86_out_block(entry, &out_ctx);
	x86_emit_epilogue(&out_ctx, exit);

	dynmap_free(alloca_ctx.alloca2stack);

	/* now we spit out the prologue first */
	x86_emit_prologue(
			func,
			out_ctx.alloca_bottom + out_ctx.spill_alloca_max,
			out_ctx.max_align);

	if(cat_file(out_ctx.fout, stdout) != 0)
		die("cat file:");

	fclose(out_ctx.fout);

	blocks_clear_flags(entry);
}

static void x86_out_var(variable *var)
{
	const char *name = variable_name(var);

	printf(".bss\n");
	printf(".globl %s\n", name);
	printf("%s: .space %u\n", name, variable_size(var));
}

void x86_out(unit *unit, global *glob)
{
	if(glob->is_fn){
		function *fn = glob->u.fn;

		if(function_entry_block(fn, false))
			x86_out_fn(unit, fn);

	}else{
		x86_out_var(glob->u.var);
	}
}
