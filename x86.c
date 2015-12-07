#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <stdint.h>

#include "macros.h"
#include "die.h"
#include "io.h"
#include "mem.h"
#include "dynmap.h"

#include "x86.h"
#include "x86_internal.h"

#include "isn_internal.h"
#include "isn_struct.h"
#include "val_struct.h"
#include "block_struct.h"
#include "function_struct.h"
#include "variable_struct.h"
#include "unit.h"
#include "unit_internal.h"
#include "val_internal.h" /* val_location() */
#include "global_struct.h"
#include "regalloc.h"

#include "x86_call.h"
#include "x86_isns.h"

#define OPERAND_SHOW_TYPE 0
#define TEMPORARY_SHOW_MOVES 0

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

#define PTR_SZ 8
#define PTR_TY i8

static const int callee_saves[] = {
	1 /* ebx */
};

enum deref_type
{
	DEREFERENCE_FALSE, /* match 0 for false */
	DEREFERENCE_TRUE,  /* match 1 for true */
	DEREFERENCE_ANY    /* for debugging - don't crash */
};


static void emit_ptradd(val *lhs, val *rhs, val *out, x86_octx *octx);

static operand_category val_category(val *v)
{
	if(val_is_mem(v))
		return OPERAND_MEM;

	switch(v->kind){
		case LITERAL:
			return OPERAND_INT;

		case GLOBAL:
		case ALLOCA:
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

static const char *x86_size_name(unsigned sz)
{
	switch(sz){
		case 1: return "byte";
		case 2: return "word";
		case 4: return "long";
		case 8: return "quad";
		default:
			assert(0 && "bad int size");
			return NULL;
	}
}

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
		default: assert(0 && "reg size too large");
	}

	return regs[reg][sz_idx];
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

void x86_comment(x86_octx *octx, const char *fmt, ...)
{
	va_list l;

	fprintf(octx->fout, "\t# ");

	va_start(l, fmt);
	vfprintf(octx->fout, fmt, l);
	va_end(l);

	fprintf(octx->fout, "\n");
}

static void assert_deref(enum deref_type got, enum deref_type expected)
{
	if(got == DEREFERENCE_ANY)
		return;
	assert(got == expected && "wrong deref");
}

static const char *x86_type_suffix(type *t)
{
	return x86_size_suffix(type_size(t));
}

static bool x86_can_infer_size(val *val)
{
	struct name_loc *loc = NULL;

	switch(val->kind){
		case ALLOCA: return false;
		case LITERAL: return false;
		case GLOBAL: return false;

		case ARGUMENT:
			loc = function_arg_loc(val->u.argument.func, val->u.argument.idx);
			break;

		case FROM_ISN:
			loc = &val->u.local.loc;
			break;

		case BACKEND_TEMP:
			loc = &val->u.temp_loc;
			break;
	}

	assert(loc);
	return loc->where == NAME_IN_REG;
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

		case ALLOCA: loc = &val->u.alloca.loc; goto loc;
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

static const char *x86_val_str_debug(val *v, int bufidx, x86_octx *octx)
{
	return x86_val_str(v, bufidx, octx, val_type(v), DEREFERENCE_ANY);
}

void x86_make_stack_slot(val *stack_slot, unsigned off, type *ty)
{
	val_temporary_init(stack_slot, ty);

	stack_slot->u.temp_loc.where = NAME_SPILT;
	stack_slot->u.temp_loc.u.off = off;
}

void x86_make_reg(val *reg, int regidx, type *ty)
{
	val_temporary_init(reg, ty);

	reg->u.temp_loc.where = NAME_IN_REG;
	reg->u.temp_loc.u.reg = regidx;
}

void x86_make_eax(val *out, type *ty)
{
	x86_make_reg(out, /* XXX: hard coded eax: */ 0, ty);
}

static void make_val_temporary_reg(val *valp, type *ty)
{
	val_temporary_init(valp, ty);

	/* use scratch register */
	valp->u.local.loc.where = NAME_IN_REG;
	valp->u.local.loc.u.reg = SCRATCH_REG;
}

static void make_val_temporary_store(
		val *from,
		val *write_to,
		operand_category from_cat,
		operand_category to_cat,
		x86_octx *octx)
{
	type *temporary_ty;

	/* move 'from' into category 'to_cat' (from 'from_cat'),
	 * saving the new value in *write_to */

	if(from_cat == to_cat){
		*write_to = *from;
		return;
	}

	assert(to_cat != OPERAND_INT);

	temporary_ty = from->ty;
	if(from->kind == GLOBAL || from->kind == ALLOCA)
		temporary_ty = type_deref(temporary_ty);

	val_temporary_init(write_to, temporary_ty);

	if(to_cat == OPERAND_REG){
		/* use scratch register */

		write_to->u.local.loc.where = NAME_IN_REG;
		write_to->u.local.loc.u.reg = SCRATCH_REG;

		assert(!octx->scratch_reg_reserved);

	}else{
		assert(to_cat == OPERAND_MEM);

		write_to->u.local.loc.where = NAME_SPILT;
		write_to->u.local.loc.u.off = 133; /* TODO */

		fprintf(stderr, "WARNING: to memory temporary - incomplete\n");
	}

	assert(val_size(write_to) == type_size(temporary_ty));
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
		const size_t nargs,
		bool *const is_exactmatch)
{
	const int max = countof(isn->constraints);
	int i, bestmatch_i = -1;

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
			orig_val_category, operand_category,
			octx);

	/* orig_val needs to be loaded before the instruction
	 * no dereference of temporary_store here - move into the temporary */
	x86_mov_deref(orig_val, temporary_store, octx, *deref_val, false);
	*deref_val = false;
}

static void ready_output(
		val *orig_val,
		val *temporary_store,
		operand_category orig_val_category,
		operand_category operand_category,
		bool *const deref_val,
		x86_octx *octx)
{
	/* wait to store the value until after the main isn */
	make_val_temporary_store(
			orig_val, temporary_store,
			orig_val_category, operand_category,
			octx);

	/* using a register as a temporary rhs - no dereference */
	*deref_val = 0;
}

static const char *maybe_generate_isn_suffix(
		const size_t operand_count, val *emit_vals[],
		emit_isn_operand operands[])
{
	size_t i;

	/* can we infer the isn size from an operand? */
	for(i = 0; i < operand_count; i++){
		if(operands[i].dereference || !x86_can_infer_size(emit_vals[i]))
				continue;

		/* size can be inferred */
		return NULL;
	}

	/* figure out the size from one of the operands */
	for(i = 0; i < operand_count; i++){
		if(operands[i].dereference)
			continue;

		return x86_type_suffix(val_type(emit_vals[i]));
	}

	assert(0 && "couldn't infer suffix for instruction");
	return NULL;
}

static bool emit_isn_try(
		const struct x86_isn *isn, x86_octx *octx,
		emit_isn_operand operands[],
		unsigned operand_count,
		const char *x86_isn_suffix)
{
	val *emit_vals[MAX_OPERANDS];
	bool orig_dereference[MAX_OPERANDS];
	operand_category op_categories[MAX_OPERANDS] = { 0 };
	struct {
		val val;
		variable var;
	} temporaries[MAX_OPERANDS];
	bool is_exactmatch;
	const struct x86_isn_constraint *operands_target = NULL;
	unsigned j;

	assert(operand_count == isn->arg_count);
	assert(operand_count <= MAX_OPERANDS);

	for(j = 0; j < operand_count; j++){
		emit_vals[j] = operands[j].val;
		orig_dereference[j] = operands[j].dereference;

		op_categories[j] = operands[j].dereference
			? OPERAND_MEM
			: val_category(operands[j].val);

		if(op_categories[j] == OPERAND_MEM)
			operands[j].dereference = true;
	}

	operands_target = find_isn_bestmatch(
			isn, op_categories, operand_count, &is_exactmatch);

	if(!operands_target)
		return false;

	if(!is_exactmatch){
		/* not satisfied - convert an operand to REG or MEM */
		if(TEMPORARY_SHOW_MOVES)
			x86_comment(octx, "temp(s) needed for %s", isn->mnemonic);

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

				if(TEMPORARY_SHOW_MOVES){
					x86_comment(octx, "^ temporary for [%zu] input: %s -> %s type %s",
							j,
							x86_val_str_debug(operands[j].val, 0, octx),
							x86_val_str_debug(emit_vals[j], 1, octx),
							type_to_str(val_type(operands[j].val)));
				}
			}

			/* ready the output operands */
			if(op_categories[j] != operands_target->category[j]
			&& isn->arg_ios[j] & OPERAND_OUTPUT)
			{
				ready_output(
						operands[j].val, &temporaries[j].val,
						op_categories[j], operands_target->category[j],
						&operands[j].dereference, octx);

				emit_vals[j] = &temporaries[j].val;

				if(TEMPORARY_SHOW_MOVES){
					x86_comment(octx, "temporary for [%zu] output: %s -> %s",
							j,
							x86_val_str_debug(operands[j].val, 0, octx),
							x86_val_str_debug(emit_vals[j], 1, octx));
				}
			}
		}
	}else{
		/* satisfied */
	}

	if(!x86_isn_suffix && isn->may_suffix)
		x86_isn_suffix = maybe_generate_isn_suffix(operand_count, emit_vals, operands);
	if(!x86_isn_suffix)
		x86_isn_suffix = "";

	fprintf(octx->fout, "\t%s%s ", isn->mnemonic, x86_isn_suffix);

	for(j = 0; j < operand_count; j++){
		type *operand_ty;
		const char *val_str;

		operand_ty = val_type(emit_vals[j]);

		val_str = x86_val_str(
				emit_vals[j], 0,
				octx,
				operand_ty,
				operands[j].dereference);

		if(OPERAND_SHOW_TYPE)
			fprintf(octx->fout, "{%s}", type_to_str(emit_vals[j]->ty));

		fprintf(octx->fout, "%s%s",
				val_str,
				j + 1 == operand_count ? "\n" : ", ");
	}


	/* store outputs after the instruction */
	for(j = 0; j < operand_count; j++){
		if(op_categories[j] != operands_target->category[j]
		&& isn->arg_ios[j] & OPERAND_OUTPUT)
		{
			x86_mov_deref(
					emit_vals[j], operands[j].val,
					octx,
					false, orig_dereference[j]);
		}
	}

	return true;
}

void x86_emit_isn(
		const struct x86_isn *isn, x86_octx *octx,
		emit_isn_operand operands[],
		unsigned operand_count,
		const char *x86_isn_suffix)
{
	bool did = emit_isn_try(isn, octx, operands, operand_count, x86_isn_suffix);

	assert(did && "couldn't satisfy operands for isn");
}

static void emit_isn_binary(
		const struct x86_isn *isn, x86_octx *octx,
		val *const lhs, bool deref_lhs,
		val *const rhs, bool deref_rhs,
		const char *x86_isn_suffix)
{
	emit_isn_operand operands[2];

	operands[0].val = lhs;
	operands[0].dereference = deref_lhs;

	operands[1].val = rhs;
	operands[1].dereference = deref_rhs;

	x86_emit_isn(isn, octx, operands, 2, x86_isn_suffix);
}

static bool must_lea_val(val *v)
{
	/* this assumes we haven't been told to dereference */

	if(v->kind == ALLOCA)
		return true;

	if(v->kind == GLOBAL)
		return true;

	return false;
}

static void mov_deref_force(
		val *from, val *to,
		x86_octx *octx,
		bool deref_from, bool deref_to,
		bool const force)
{
	const struct x86_isn *chosen_isn = &x86_isn_mov;

	if(!force && !deref_from && !deref_to){
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

	/* if we're x86_mov:ing from a non-lvalue (i.e. array, struct [alloca])
	 * we actually want its address*/
	if(!deref_from && must_lea_val(from)){
		chosen_isn = &x86_isn_lea;
	}

	emit_isn_binary(chosen_isn, octx,
			from, deref_from,
			to, deref_to,
			NULL);
}

void x86_mov_deref(
		val *from, val *to,
		x86_octx *octx,
		bool deref_from, bool deref_to)
{
	mov_deref_force(from, to, octx, deref_from, deref_to, 0);
}

void x86_mov(val *from, val *to, x86_octx *octx)
{
	x86_mov_deref(from, to, octx, false, false);
}

static bool elem_get_offset(val *maybe_int, type *elem_ty, long *const offset)
{
	if(maybe_int->kind != LITERAL)
		return false;

	*offset = maybe_int->u.i * type_size(type_deref(elem_ty));

	return true;
}

static void emit_elem(isn *i, x86_octx *octx)
{
	val *const lval = i->u.elem.lval;

	switch(lval->kind){
			struct name_loc *loc;

		case LITERAL:
			break;

		case ARGUMENT:
			loc = function_arg_loc(lval->u.argument.func, lval->u.argument.idx);
			goto loc;
		case ALLOCA:
			loc = &lval->u.alloca.loc;
			goto loc;
		case FROM_ISN:
			loc = &lval->u.local.loc;
			goto loc;
		case BACKEND_TEMP:
			loc = &lval->u.temp_loc;
			goto loc;

loc:
		{
			long offset;
			if(!elem_get_offset(i->u.elem.index, val_type(i->u.elem.res), &offset))
				break;

			switch(loc->where){
				case NAME_IN_REG:
				{
					const char *pointer_str = x86_val_str(
							i->u.elem.lval, 0, octx,
							val_type(i->u.elem.lval),
							DEREFERENCE_FALSE);

					const char *result_str = x86_val_str(
							i->u.elem.res, 1, octx,
							val_type(i->u.elem.res),
							DEREFERENCE_FALSE);

					fprintf(octx->fout, "\tlea %ld(%s), %s\n",
							offset,
							pointer_str,
							result_str);
					return;
				}

				case NAME_SPILT:
					break;
			}
			break;
		}

		case GLOBAL:
		{
			const char *result_str;
			long offset;

			if(!elem_get_offset(i->u.elem.index, val_type(i->u.elem.res), &offset))
				break;

			result_str = x86_val_str(
					i->u.elem.res, 1, octx,
					val_type(i->u.elem.res),
					DEREFERENCE_FALSE);

			fprintf(octx->fout, "\tlea %s+%ld(%%rip), %s\n",
					global_name(lval->u.global),
					offset,
					result_str);
			return;
		}
	}

	/* worst case - emit an add */
	fprintf(octx->fout, "\t; worst case lea\n");
	emit_ptradd(lval, i->u.elem.index, i->u.elem.res, octx);
}

static void x86_op(
		enum op op, val *lhs, val *rhs,
		val *res, x86_octx *octx)
{
	struct x86_isn opisn;

	if(op == op_mul){
		/* use the three-operand mode */
		emit_isn_operand operands[3] = { 0 };

		operands[0].val = lhs;
		operands[1].val = rhs;
		operands[2].val = res;

		if(emit_isn_try(&x86_isn_imul, octx, operands, 3, NULL)){
			return;
		}

		/* try swapping lhs and rhs */
		operands[0].val = rhs;
		operands[1].val = lhs;
		if(emit_isn_try(&x86_isn_imul, octx, operands, 3, NULL)){
			return;
		}

		/* else fall back to 2-operand mode */
	}

	opisn = x86_isn_add;

	switch(op){
		case op_add:
			break;
		case op_sub:
			opisn.mnemonic = "sub";
			break;
		case op_mul:
			opisn.mnemonic = "imul";
			break;
		case op_shiftl:       opisn.mnemonic = "shl"; break;
		case op_shiftr_arith: opisn.mnemonic = "sar"; break;
		case op_shiftr_logic: opisn.mnemonic = "shr"; break;

		default:
			assert(0 && "TODO: other ops");
	}

	/* no instruction selection / register merging. just this for now */
	x86_comment(octx, "pre-op x86_mov:");
	x86_mov(lhs, res, octx);

	emit_isn_binary(&opisn, octx, rhs, false, res, false, NULL);
}

static void x86_ext(val *from, val *to, const bool sign, x86_octx *octx)
{
	unsigned sz_from = val_size(from);
	unsigned sz_to = val_size(to);
	char buf[4] = { 0 };
	struct name_loc *from_loc = val_location(from);
	struct name_loc *to_loc = val_location(to);

	/* zext requires something in a reg */
	assert(from_loc && from_loc->where == NAME_IN_REG
			&& to_loc && to_loc->where == NAME_IN_REG);

	buf[0] = (sign ? 's' : 'z');

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
		if(sign){
			buf[1] = 'l';
		}else{
			val to_shrunk;

			to_shrunk = *to;

			to_shrunk.ty = type_get_primitive(unit_uniqtypes(octx->unit), i4);

			assert(sz_to == 8);
			x86_comment(octx, "zext:");
			mov_deref_force(from, &to_shrunk, octx, false, false, /*force:*/true);
			return;
		}
		break;

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

	emit_isn_binary(&x86_isn_movzx, octx,
			from, false, to, false, buf);
}

static void emit_ptradd(val *lhs, val *rhs, val *out, x86_octx *octx)
{
	const unsigned ptrsz = type_size(val_type(lhs));
	const unsigned step = type_size(type_deref(val_type(lhs)));
	type *rhs_ty = val_type(rhs);
	type *intptr_ty = type_get_primitive(
			unit_uniqtypes(octx->unit),
			PTR_TY);

	val ext_rhs;
	val reg;

	if(type_size(rhs_ty) != ptrsz){
		bool need_temp_reg = false;
		val ext_from_temp;

		assert(type_size(rhs_ty) < ptrsz);

		switch(rhs->kind){
			case LITERAL:
				/* fine */
				break;
			case GLOBAL:
				need_temp_reg = true;
				break;
			case ARGUMENT:
			case FROM_ISN:
			case ALLOCA:
			case BACKEND_TEMP:
			{
				struct name_loc *loc = val_location(rhs);
				need_temp_reg = (!loc || loc->where == NAME_SPILT);
				break;
			}
		}

		if(need_temp_reg){
			/* use scratch for the ext */
			x86_make_reg(&ext_rhs, SCRATCH_REG, val_type(lhs));

			/* smaller scratch */
			ext_from_temp = ext_rhs;
			ext_from_temp.ty = rhs_ty;

			/* populate smaller scratch */
			x86_mov(rhs, &ext_from_temp, octx);

			/* extend to bigger scratch */
			x86_ext(&ext_from_temp, &ext_rhs, /*sign:*/false, octx);

		}else{
			ext_rhs = *rhs;
			ext_rhs.ty = intptr_ty;
			x86_ext(rhs, &ext_rhs, /*sign:*/false, octx);
		}

		rhs = &ext_rhs;
	}

	if(step != 1){
		val multiplier;

		assert(step > 0);

		val_temporary_init(&multiplier, intptr_ty);
		multiplier.kind = LITERAL;
		multiplier.u.i = step;

		make_val_temporary_reg(&reg, intptr_ty);
		x86_op(op_mul, &multiplier, rhs, &reg, octx);

		rhs = &reg;
	}

	x86_op(op_add, lhs, rhs, out, octx);
}

static void x86_cmp(
		enum op_cmp cmp,
		val *lhs, val *rhs, val *res,
		x86_octx *octx)
{
	val *zero;
	emit_isn_operand set_operand;

	emit_isn_binary(&x86_isn_cmp, octx,
			lhs, false,
			rhs, false,
			NULL);

	zero = val_retain(val_new_i(0, res->ty));

	x86_mov(zero, res, octx);

	set_operand.val = res;
	set_operand.dereference = false;

	x86_emit_isn(&x86_isn_set, octx, &set_operand, 1, x86_cmp_str(cmp));

	val_release(zero);
}

static void x86_jmp(x86_octx *octx, block *target)
{
	fprintf(octx->fout, "\tjmp %s%s\n",
			unit_lbl_private_prefix(octx->unit),
			target->lbl);
}

static void x86_branch(val *cond, block *bt, block *bf, x86_octx *octx)
{
	emit_isn_binary(&x86_isn_test, octx,
			cond, false,
			cond, false,
			NULL);

	fprintf(octx->fout, "\tjz %s%s\n",
			unit_lbl_private_prefix(octx->unit),
			bf->lbl);

	x86_jmp(octx, bt);
}

static void x86_block_enter(x86_octx *octx, block *blk)
{
	if(!blk->lbl)
		return;

	fprintf(octx->fout, "%s%s:\n",
			unit_lbl_private_prefix(octx->unit),
			blk->lbl);
}

static void x86_ptr2int(val *from, val *to, x86_octx *octx)
{
	/* if the type sizes are different we need to do a bit of wrangling */
	type *ty_from = val_type(from), *ty_to = val_type(to);
	unsigned sz_from = type_size(ty_from), sz_to = type_size(ty_to);

	if(sz_from < sz_to){
		x86_ext(from, to, /*sign:*/false, octx);
	}else if(sz_from > sz_to){
		/* trunc */
		val trunc = *from;

		x86_comment(octx, "truncate");
		trunc.ty = ty_to;
		x86_mov(&trunc, to, octx);

	}else{
		x86_mov(from, to, octx);
	}
}

static void x86_ptrcast(val *from, val *to, x86_octx *octx)
{
	type *ty_from = val_type(from), *ty_to = val_type(to);
	unsigned sz_from = type_size(ty_from), sz_to = type_size(ty_to);

	assert(sz_from == sz_to);

	x86_mov(from, to, octx);
}

static void x86_out_block1(block *blk, void *vctx)
{
	x86_octx *const octx = vctx;
	isn *head = block_first_isn(blk);
	isn *i;
	unsigned idx;

	if(blk->emitted)
		return;
	blk->emitted = 1;

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
					x86_make_eax(&veax, i->u.ret->ty);

					x86_mov(i->u.ret, &veax, octx);
				}

				fprintf(octx->fout, "\tjmp %s%s\n",
						unit_lbl_private_prefix(octx->unit),
						octx->exitblk->lbl);
				break;
			}

			case ISN_STORE:
			{
				x86_mov_deref(i->u.store.from, i->u.store.lval, octx, false, true);
				break;
			}

			case ISN_LOAD:
			{
				x86_mov_deref(i->u.load.lval, i->u.load.to, octx, true, false);
				break;
			}

			case ISN_ELEM:
				emit_elem(i, octx);
				break;

			case ISN_PTRADD:
				emit_ptradd(i->u.ptradd.lhs, i->u.ptradd.rhs, i->u.ptradd.out, octx);
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
				x86_ext(i->u.ext.from, i->u.ext.to, i->u.ext.sign, octx);
				break;

			case ISN_INT2PTR:
			case ISN_PTR2INT:
				x86_ptr2int(i->u.ptr2int.from, i->u.ptr2int.to, octx);
				break;

			case ISN_PTRCAST:
				x86_ptrcast(i->u.ptrcast.from, i->u.ptrcast.to, octx);
				break;

			case ISN_COPY:
			{
				x86_mov(i->u.copy.from, i->u.copy.to, octx);
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
				x86_emit_call(blk, idx,
						i->u.call.into_or_null,
						i->u.call.fn,
						&i->u.call.args,
						octx);
				break;
			}
		}
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

static void x86_init_regalloc_info(
		struct regalloc_info *info,
		function *func,
		uniq_type_list *uniq_type_list)
{
	info->backend.nregs = countof(regs);
	info->backend.scratch_reg = SCRATCH_REG;
	info->backend.ptrsz = PTR_SZ;
	info->backend.callee_save = callee_saves;
	info->backend.callee_save_cnt = countof(callee_saves);
	info->backend.arg_regs = x86_arg_regs;
	info->backend.arg_regs_cnt = x86_arg_reg_count;
	info->func = func;
	info->uniq_type_list = uniq_type_list;
}

static long x86_alloca_total(x86_octx *octx)
{
	return octx->alloca_bottom + octx->spill_alloca_max;
}

static void x86_out_fn(unit *unit, function *func)
{
	struct x86_alloca_ctx alloca_ctx = { 0 };
	x86_octx out_ctx = { 0 };
	block *const entry = function_entry_block(func, false);
	block *const exit = function_exit_block(func);
	struct regalloc_info regalloc;
	dynmap *markers = BLOCK_DYNMAP_NEW();

	out_ctx.unit = unit;

	out_ctx.fout = tmpfile();
	if(!out_ctx.fout)
		die("tmpfile():");

	alloca_ctx.alloca2stack = dynmap_new(val *, /*ref*/NULL, val_hash);

	/* regalloc */
	x86_init_regalloc_info(&regalloc, func, unit_uniqtypes(unit));
	func_regalloc(func, &regalloc);

	/* gather allocas - must be after regalloc */
	blocks_traverse(entry, x86_sum_alloca, &alloca_ctx, markers);

	out_ctx.alloca2stack = alloca_ctx.alloca2stack;
	out_ctx.exitblk = exit;
	out_ctx.func = func;

	/* start at the bottom of allocas */
	out_ctx.alloca_bottom = alloca_ctx.alloca;

	blocks_traverse(entry, x86_out_block1, &out_ctx, markers);
	x86_emit_epilogue(&out_ctx, exit);

	dynmap_free(alloca_ctx.alloca2stack);

	/* now we spit out the prologue first */
	x86_emit_prologue(
			func,
			x86_alloca_total(&out_ctx),
			out_ctx.max_align);

	if(cat_file(out_ctx.fout, stdout) != 0)
		die("cat file:");

	fclose(out_ctx.fout);

	dynmap_free(markers);
}

static void x86_out_var(variable_global *var)
{
	variable *inner = variable_global_var(var);
	const char *name = variable_name(inner);
	struct init *init = variable_global_init(var);

	/* TODO: use .bss for zero-init */
	printf(".data\n");
	printf(".globl %s\n", name);
	printf("%s:\n", name);

	if(init){
		switch(init->type){
			case init_int:
				printf(".%s %#llx\n",
						x86_size_name(variable_size(inner)),
						init->u.i);
				break;

			case init_str:
			{
				printf(".ascii \"");
				dump_escaped_string(&init->u.str);
				printf("\"\n");
				break;
			}

			default:
				assert(0 && "TODO: missing init");
		}
	}else{
		printf(".space %u\n", variable_size(inner));
	}
}

void x86_out(unit *unit, global *glob)
{
	if(global_is_forward_decl(glob)){
		x86_octx octx = { 0 };
		octx.fout = stdout;
		x86_comment(&octx, "forward decl %s", global_name(glob));
		return;
	}

	if(glob->is_fn){
		function *fn = glob->u.fn;

		/* should have entry block, otherwise it's a forward decl */
		assert(function_entry_block(fn, false));
		x86_out_fn(unit, fn);

	}else{
		x86_out_var(glob->u.var);
	}
}
