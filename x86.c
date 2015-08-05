#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>

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
	unsigned long spill_alloca_max; /* max of spill space */
	unsigned max_align;
} x86_octx;

struct x86_spill_ctx
{
	dynmap *alloca2stack;
	dynmap *dontspill;
	dynmap *spill;
	block *blk;
	unsigned long spill_alloca;
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

#define MAX_OPERANDS 3
#define MAX_ISN_COMBOS 5

struct x86_isn
{
	const char *mnemonic;

	unsigned arg_count;

	enum {
		OPERAND_INPUT = 1 << 0,
		OPERAND_OUTPUT = 1 << 1
	} arg_ios[MAX_OPERANDS];

	struct x86_isn_constraint
	{
		operand_category category[MAX_OPERANDS];
	} constraints[MAX_ISN_COMBOS];
};

static const struct x86_isn isn_mov = {
	"mov",
	2,
	{
		OPERAND_INPUT,
		OPERAND_INPUT | OPERAND_OUTPUT,
		0
	},
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
	2,
	{
		OPERAND_INPUT,
		OPERAND_INPUT | OPERAND_OUTPUT,
		0
	},
	{
		{ OPERAND_REG, OPERAND_REG },
		{ OPERAND_REG, OPERAND_MEM },
	}
};

static const struct x86_isn isn_add = {
	"add",
	2,
	{
		OPERAND_INPUT,
		OPERAND_INPUT | OPERAND_OUTPUT,
		0
	},
	{
		{ OPERAND_REG, OPERAND_REG },
		{ OPERAND_REG, OPERAND_MEM },
		{ OPERAND_MEM, OPERAND_REG },
		{ OPERAND_INT, OPERAND_REG }
	}
};

static const struct x86_isn isn_cmp = {
	"cmp",
	2,
	{
		OPERAND_INPUT,
		OPERAND_INPUT,
		0
	},
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
	2,
	{
		OPERAND_INPUT,
		OPERAND_INPUT,
		0
	},
	{
		{ OPERAND_REG, OPERAND_REG },
		{ OPERAND_REG, OPERAND_MEM },
		{ OPERAND_MEM, OPERAND_REG },
		{ OPERAND_INT, OPERAND_REG },
		{ OPERAND_INT, OPERAND_MEM },
	}
};

static const struct x86_isn isn_call = {
	"call",
	1,
	{
		OPERAND_INPUT,
		0,
		0
	},
	{
		{ OPERAND_REG },
		{ OPERAND_MEM },
	}
};

static const struct x86_isn isn_set = {
	"set",
	1,
	{
		OPERAND_OUTPUT,
		0,
		0
	},
	{
		{ OPERAND_REG },
		{ OPERAND_MEM }, /* 1-byte */
	}
};

typedef struct emit_isn_operand {
	val *val;
	bool dereference;
} emit_isn_operand;

static void mov_deref(
		val *from, val *to,
		x86_octx *,
		bool dl, bool dr);



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

static const char *name_in_reg_str(const struct name_loc *loc, const int size)
{
	int sz_idx;
	int reg = loc->u.reg;

	assert(loc->where == NAME_IN_REG);

	if(reg == -1)
		return NULL;

	assert(reg < (int)countof(regs));

	switch(size){
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
		enum deref_type dereference_ty,
		type *ty)
{
	switch(loc->where){
		case NAME_IN_REG:
		{
			const bool deref = (dereference_ty == DEREFERENCE_TRUE);
			int val_sz;

			if(deref){
				val_sz = PTR_SZ;
			}else{
				val_sz = type_size(ty);
			}

			snprintf(buf, bufsz, "%s%%%s%s",
					deref ? "(" : "",
					name_in_reg_str(loc, val_sz),
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

static const char *x86_val_str(
		val *val, int bufchoice,
		x86_octx *octx,
		type *operand_output_ty,
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
					operand_output_ty);
	}

	return buf;
}

static void make_stack_slot(val *stack_slot, unsigned off, type *ty)
{
	val_temporary_init(stack_slot, ty);

	stack_slot->u.temp_loc.where = NAME_SPILT;
	stack_slot->u.temp_loc.u.off = off;
}

attr_nonnull()
static void make_reg(val *reg, int regidx, type *ty)
{
	val_temporary_init(reg, ty);

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
		operand_category to_cat)
{
	/* move 'from' into category 'to_cat' (from 'from_cat'),
	 * saving the new value in *write_to */

	if(from_cat == to_cat){
		*write_to = *from;
		return;
	}

	assert(to_cat != OPERAND_INT);

	val_temporary_init(write_to, from->ty);

	if(to_cat == OPERAND_REG){
		/* use scratch register */

		write_to->u.local.loc.where = NAME_IN_REG;
		write_to->u.local.loc.u.reg = SCRATCH_REG;

	}else{
		assert(to_cat == OPERAND_MEM);

		write_to->u.local.loc.where = NAME_SPILT;
		write_to->u.local.loc.u.off = 133; /* TODO */

		fprintf(stderr, "WARNING: to memory temporary - incomplete\n");
	}

	assert(val_size(write_to) == val_size(from));
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
		const operand_category arg_cats[],
		bool *const is_exactmatch)
{
	const int max = countof(isn->constraints);
	int i, bestmatch_i = -1;
	unsigned nargs;

	for(nargs = 0; arg_cats[nargs]; nargs++);

	for(i = 0; i < max && isn->constraints[i].category[0]; i++){
		bool matches[MAX_OPERANDS];
		unsigned nmatches = 0;
		unsigned conversions_required;
		unsigned j;

		for(j = 0; j < nargs; j++){
			if(j == isn->arg_count)
				break;

			matches[j] = (arg_cats[j] == isn->constraints[i].category[j]);

			if(matches[j])
				nmatches++;
		}

		conversions_required = (nargs - nmatches);

		if(conversions_required == 0){
			*is_exactmatch = true;
			return &isn->constraints[i];
		}

		if(bestmatch_i != -1)
			continue;

		if(conversions_required > 1){
			/* don't attempt, for now */
			continue;
		}

		/* we can only have the best match if the non-matched operand
		 * is convertible to the required operand type */
		for(j = 0; j < nargs; j++){
			if(matches[j])
				continue;

			if(operand_type_convertible(
						arg_cats[j], isn->constraints[i].category[j]))
			{
				bestmatch_i = i;
				break;
			}
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
		bool *const deref_val,
		x86_octx *octx)
{
	make_val_temporary_store(
			orig_val, temporary_store,
			orig_val_category, operand_category);

	/* orig_val needs to be loaded before the instruction
	 * no dereference of temporary_store here - move into the temporary */
	mov_deref(orig_val, temporary_store, octx, *deref_val, false);
	*deref_val = false;
}

static void ready_output(
		val *orig_val,
		val *temporary_store,
		operand_category orig_val_category,
		operand_category operand_category,
		bool *const deref_val)
{
	/* wait to store the value until after the main isn */
	make_val_temporary_store(
			orig_val, temporary_store,
			orig_val_category, operand_category);

	/* using a register as a temporary rhs - no dereference */
	*deref_val = 0;
}

static void emit_isn(
		const struct x86_isn *isn, x86_octx *octx,
		emit_isn_operand operands[],
		unsigned operand_count,
		const char *isn_suffix)
{
	val *emit_vals[MAX_OPERANDS];
	bool orig_dereference[MAX_OPERANDS];
	operand_category op_categories[MAX_OPERANDS] = { 0 };
	struct {
		val val;
		variable var;
	} temporaries[MAX_OPERANDS];
	bool is_exactmatch;
	bool handling_global = false;
	const struct x86_isn_constraint *operands_target = NULL;
	unsigned j;

	for(j = 0; j < operand_count; j++){
		emit_vals[j] = operands[j].val;
		orig_dereference[j] = operands[j].dereference;

		op_categories[j] = operands[j].dereference
			? OPERAND_MEM
			: val_category(operands[j].val);

		if(op_categories[j] == OPERAND_MEM)
			operands[j].dereference = true;
	}

	operands_target = find_isn_bestmatch(isn, op_categories, &is_exactmatch);

	assert(operands_target && "couldn't satisfy operands for isn");

	if(!is_exactmatch){
		/* not satisfied - convert an operand to REG or MEM */

		for(j = 0; j < operand_count; j++){
			/* ready the operands if they're inputs */
			if(isn->arg_ios[j] & OPERAND_INPUT
			&& op_categories[j] != operands_target->category[j])
			{
				ready_input(
						operands[j].val, &temporaries[j].val,
						op_categories[j], operands_target->category[j],
						&operands[j].dereference, octx);

				emit_vals[j] = &temporaries[j].val;
			}

			/* ready the output operands */
			if(op_categories[j] != operands_target->category[j]
			&& isn->arg_ios[j] & OPERAND_OUTPUT)
			{
				ready_output(
						operands[j].val, &temporaries[j].val,
						op_categories[j], operands_target->category[j],
						&operands[j].dereference);

				emit_vals[j] = &temporaries[j].val;
			}
		}
	}else{
		/* satisfied */
	}

	fprintf(octx->fout, "\t%s%s ", isn->mnemonic, isn_suffix);

	for(j = 0; j < operand_count; j++){
		if(operands[j].val->kind == GLOBAL){
			handling_global = true;
			break;
		}
	}

	for(j = 0; j < operand_count; j++){
		type *operand_ty;
		const char *val_str;

		operand_ty = val_type(emit_vals[j]);

		if(handling_global){
			type *next = type_deref(operand_ty);
			if(next)
				operand_ty = next;
		}

		val_str = x86_val_str(
				emit_vals[j], 0,
				octx,
				operand_ty,
				operands[j].dereference);

		fprintf(octx->fout, "%s%s",
				val_str,
				j + 1 == operand_count ? "\n" : ", ");
	}


	/* store outputs after the instruction */
	for(j = 0; j < operand_count; j++){
		if(op_categories[j] != operands_target->category[j]
		&& isn->arg_ios[j] & OPERAND_OUTPUT)
		{
			mov_deref(
					emit_vals[j], operands[j].val,
					octx,
					false, orig_dereference[j]);
		}
	}
}

static void emit_isn_binary(
		const struct x86_isn *isn, x86_octx *octx,
		val *const lhs, bool deref_lhs,
		val *const rhs, bool deref_rhs,
		const char *isn_suffix)
{
	emit_isn_operand operands[2];

	operands[0].val = lhs;
	operands[0].dereference = deref_lhs;

	operands[1].val = rhs;
	operands[1].dereference = deref_rhs;

	emit_isn(isn, octx, operands, 2, isn_suffix);
}

static void mov_deref(
		val *from, val *to,
		x86_octx *octx,
		bool dl, bool dr)
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

	emit_isn_binary(&isn_mov, octx,
			from, dl,
			to, dr,
			"");
}

static void mov(val *from, val *to, x86_octx *octx)
{
	mov_deref(from, to, octx, false, false);
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

	emit_isn_binary(&opisn, octx, rhs, false, res, false, "");
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

	emit_isn_binary(&isn_movzx, octx,
			from, false, to, false, buf);
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
	emit_isn_operand set_operand;

	emit_isn_binary(&isn_cmp, octx,
			lhs, false,
			rhs, false,
			x86_size_suffix(val_size(lhs)));

	zero = val_retain(val_new_i(0, lhs->ty));

	mov(zero, res, octx);

	set_operand.val = res;
	set_operand.dereference = false;

	emit_isn(&isn_set, octx, &set_operand, 1, x86_cmp_str(cmp));

	val_release(zero);
}

static void x86_jmp(x86_octx *octx, block *target)
{
	fprintf(octx->fout, "\tjmp %s\n", target->lbl);
}

static void x86_branch(val *cond, block *bt, block *bf, x86_octx *octx)
{
	emit_isn_binary(&isn_test, octx,
			cond, false,
			cond, false,
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

static void gather_for_spill(val *v, const struct x86_spill_ctx *ctx)
{
	const struct lifetime lt_inf = LIFETIME_INIT_INF;
	const struct lifetime *lt;

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

static void maybe_gather_for_spill(val *v, isn *isn, void *vctx)
{
	const struct x86_spill_ctx *ctx = vctx;

	(void)isn;

	switch(v->kind){
		case LITERAL:
		case GLOBAL:
		case BACKEND_TEMP:
			return; /* no need to spill */

		case FROM_ISN:
		case ARGUMENT:
			break;
	}

	gather_for_spill(v, ctx);
}

static void gather_live_vals(block *blk, struct x86_spill_ctx *spillctx)
{
	isn *isn;
	for(isn = block_first_isn(blk); isn; isn = isn->next){
		if(isn->skip)
			continue;

		isn_on_live_vals(isn, maybe_gather_for_spill, spillctx);
	}
}

static void spill_vals(x86_octx *octx, struct x86_spill_ctx *spillctx)
{
	size_t idx;
	val *v;

	for(idx = 0; (v = dynmap_key(val *, spillctx->spill, idx)); idx++){
		val stack_slot = { 0 };

		spillctx->spill_alloca += type_size(v->ty);

		make_stack_slot(
				&stack_slot,
				octx->alloca_bottom + spillctx->spill_alloca,
				v->ty);

		fprintf(octx->fout, "\t# spill '%s'\n", val_str(v));

		mov_deref(v, &stack_slot, octx, false, true);

		assert(stack_slot.kind == BACKEND_TEMP);
		assert(stack_slot.u.temp_loc.where == NAME_SPILT);
		dynmap_set(
				val *, uintptr_t,
				spillctx->spill,
				v,
				(uintptr_t)stack_slot.u.temp_loc.u.off);
	}
}

static void find_args_in_isn(val *v, isn *isn, void *vctx)
{
	struct x86_spill_ctx *spillctx = vctx;

	(void)isn;

	if(v->kind != ARGUMENT)
		return;

	gather_for_spill(v, spillctx);
}

static void find_args_in_block(block *blk, void *vctx)
{
	struct x86_spill_ctx *spillctx = vctx;

	isn_on_live_vals(block_first_isn(blk), find_args_in_isn, spillctx);
}

static void gather_arg_vals(function *func, struct x86_spill_ctx *spillctx)
{
	/* can't just spill regs in this block, need to spill 'live' regs,
	 * e.g.  argument regs
	 * we only spill arguments that are actually used, for the moment,
	 * that's any argument used at all, regardless of blocks
	 */
	block *blk = function_entry_block(func, false);
	assert(blk);

	blocks_traverse(blk, find_args_in_block, spillctx);
}

static dynmap *x86_spillregs(
		block *blk,
		val *except[],
		unsigned call_isn_idx,
		x86_octx *octx)
{
	struct x86_spill_ctx spillctx = { 0 };
	val **vi;

	spillctx.spill     = dynmap_new(val *, NULL, val_hash);
	spillctx.dontspill = dynmap_new(val *, NULL, val_hash);
	spillctx.alloca2stack = octx->alloca2stack;
	spillctx.call_isn_idx = call_isn_idx;
	spillctx.blk = blk;

	for(vi = except; *vi; vi++){
		dynmap_set(val *, void *, spillctx.dontspill, *vi, (void *)NULL);
	}

	gather_live_vals(blk, &spillctx);
	gather_arg_vals(octx->func, &spillctx);

	spill_vals(octx, &spillctx);

	if(spillctx.spill_alloca > octx->spill_alloca_max)
		octx->spill_alloca_max = spillctx.spill_alloca;

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

		mov_deref(&stack_slot, v, octx, true, false);
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
		emit_isn_operand operand;

		operand.val = fn;
		operand.dereference = false;

		emit_isn(&isn_call, octx, &operand, 1, " *");
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
				mov_deref(i->u.store.from, i->u.store.lval, octx, false, true);
				break;
			}

			case ISN_LOAD:
			{
				mov_deref(i->u.load.lval, i->u.load.to, octx, true, false);
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
		function *func,
		uniq_type_list *uniq_type_list)
{
	ctx->backend.nregs = countof(regs);
	ctx->backend.scratch_reg = SCRATCH_REG;
	ctx->backend.ptrsz = PTR_SZ;
	ctx->backend.callee_save = callee_saves;
	ctx->backend.callee_save_cnt = countof(callee_saves);
	ctx->backend.arg_regs = arg_regs;
	ctx->backend.arg_regs_cnt = countof(arg_regs);
	ctx->func = func;
	ctx->uniq_type_list = uniq_type_list;
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
	x86_init_regalloc_context(&regalloc, func, unit_uniqtypes(unit));
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
