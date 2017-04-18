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
#include "imath.h"

#include "x86.h"
#include "x86_internal.h"
#include "target.h"

#include "isn.h"
#include "isn_struct.h"
#include "val_struct.h"
#include "block_struct.h"
#include "function_struct.h"
#include "variable_struct.h"
#include "unit.h"
#include "unit_internal.h"
#include "val_internal.h" /* val_location() */
#include "global_struct.h"

#include "x86_call.h"
#include "x86_isns.h"

#define OPERAND_SHOW_TYPE 0
#define TEMPORARY_SHOW_MOVES 1
#define USER_LABEL_FORMAT "%s_%s"
#define X86_COMMENT_STR "#"

static const char *const regs[][4] = {
	{  "al", "ax", "eax", "rax" },
	{  "bl", "bx", "ebx", "rbx" },
	{  "cl", "cx", "ecx", "rcx" },
	{  "dl", "dx", "edx", "rdx" },
	{ "dil", "di", "edi", "rdi" },
	{ "sil", "si", "esi", "rsi" },
};

#if 0
static const int callee_saves[] = {
	1 /* ebx */
};
#endif

enum deref_type
{
	DEREFERENCE_FALSE, /* match 0 for false */
	DEREFERENCE_TRUE,  /* match 1 for true */
	DEREFERENCE_ANY    /* for debugging - don't crash */
};


static int x86_target_switch(const struct target *target, int b32, int b64)
{
	switch(target->arch.ptr.size){
		case 4: return b32;
		case 8: return b64;
		default:
			assert(0 && "invalid pointer size for x86 backend");
	}
	return -1;
}

static char x86_target_regch(const struct target *target)
{
	return x86_target_switch(target, 'e', 'r');
}

static operand_category val_category(val *v, bool dereference)
{
	switch(v->kind){
		case LITERAL:
			return dereference ? OPERAND_MEM_CONTENTS : OPERAND_INT;

		case GLOBAL:
		case ALLOCA:
			return dereference ? OPERAND_MEM_CONTENTS : OPERAND_MEM_PTR;

		case ABI_TEMP:
			return OPERAND_REG;

		case BACKEND_TEMP:
		case ARGUMENT:
		case FROM_ISN:
			return OPERAND_REG;
	}
	assert(0);
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

static const char *name_in_reg_str(const struct location *loc, const int size)
{
	int sz_idx;
	regt reg = loc->u.reg;

	assert(loc->where == NAME_IN_REG);

	if(!regt_is_valid(reg))
		return NULL;

	assert(reg < (int)countof(regs));

	switch(size){
		case 1: sz_idx = 0; break;
		case 2: sz_idx = 1; break;
		case 4: sz_idx = 2; break;
		case 8: sz_idx = 3; break;
		default: assert(0 && "reg size too large");
	}

	return regs[regt_index(reg)][sz_idx];
}

static const char *x86_cmp_str(enum op_cmp cmp)
{
	switch(cmp){
		case cmp_eq: return "e";
		case cmp_ne: return "ne";
		case cmp_gt: return "g";
		case cmp_ge: return "ge";
		case cmp_lt: return "l";
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

	fprintf(octx->fout, "\t" X86_COMMENT_STR " ");

	va_start(l, fmt);
	vfprintf(octx->fout, fmt, l);
	va_end(l);

	fprintf(octx->fout, "\n");
}

static void assert_deref(enum deref_type got, enum deref_type expected)
{
	if(got == DEREFERENCE_ANY)
		return;
	/*assert(got == expected && "wrong deref");*/
	if(got != expected){
		fprintf(stderr, "/* \x1b[1;31massert_deref() failed\x1b[m */\n");
	}
}

static const char *x86_type_suffix(type *t)
{
	return x86_size_suffix(type_size(t));
}

static bool x86_can_infer_size(val *val)
{
	struct location *loc = NULL;

	switch(val->kind){
		case ALLOCA: return false;
		case LITERAL: return false;
		case GLOBAL: return false;

		case ABI_TEMP:
			loc = &val->u.abi;
			break;

		case ARGUMENT:
			loc = &val->u.argument.loc;
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
		const struct location *loc,
		char *buf, size_t bufsz,
		enum deref_type dereference_ty,
		type *ty, const struct target *target)
{
	switch(loc->where){
		case NAME_NOWHERE:
			assert(0 && "NAME_NOWHERE in backend");

		case NAME_IN_REG:
		{
			const bool deref = (dereference_ty == DEREFERENCE_TRUE);
			int val_sz;

			if(deref){
				val_sz = target->arch.ptr.size;
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
		{
			char regch = x86_target_regch(target);

			assert_deref(dereference_ty, DEREFERENCE_TRUE);

			snprintf(buf, bufsz, "-%u(%%%cbp)", loc->u.off, regch);
			break;
		}
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
		struct location *loc;

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
			const struct target *target = unit_target_info(octx->unit);
			const int x64 = x86_target_switch(target, 0, 1);

			/* x64 is just PIC at the moment
			 * x86 is not PIC
			 */
			/* TODO: pic */
			snprintf(buf, sizeof bufs[0],
					"%s%s%s",
					indir ? "" : "$",
					global_name(val->u.global),
					x64 ? "(%rip)" : "");
			break;
		}

		case ARGUMENT: loc = &val->u.argument.loc; goto loc;
		case ALLOCA: loc = &val->u.alloca.loc; goto loc;
		case FROM_ISN: loc = &val->u.local.loc; goto loc;
		case BACKEND_TEMP: loc = &val->u.temp_loc; goto loc;
		case ABI_TEMP: loc = &val->u.abi; goto loc;
loc:
		{
			return x86_name_str(
					loc,
					buf, sizeof bufs[0],
					dereference,
					operand_output_ty,
					unit_target_info(octx->unit));
		}
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
	reg->u.temp_loc.u.reg = regidx; /* FIXME: regt indexing */
}

void x86_make_eax(val *out, type *ty)
{
	x86_make_reg(out, /* XXX: hard coded eax: */ 0, ty);
}

static void make_val_temporary_store(
		val *from,
		val *write_to,
		operand_category from_cat,
		operand_category to_cat,
		bool const deref_val,
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
	if(deref_val)
		temporary_ty = type_deref(temporary_ty);

	val_temporary_init(write_to, temporary_ty);

	if(to_cat == OPERAND_REG){
		/* use scratch register */

		write_to->u.local.loc.where = NAME_IN_REG;
		write_to->u.local.loc.u.reg = SCRATCH_REG; /* FIXME: regt indexing */

		assert(!octx->scratch_reg_reserved);

	}else{
		assert(to_cat == OPERAND_MEM_CONTENTS || to_cat == OPERAND_MEM_PTR);
		/* need to handle both of ^ */

		write_to->u.local.loc.where = NAME_SPILT;
		write_to->u.local.loc.u.off = 133; /* TODO */

		assert(0 && "WARNING: to memory temporary - incomplete");
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
		unsigned *const out_conversions_required)
{
	const int max = countof(isn->constraints);
	int i, bestmatch_i = -1;
	unsigned bestmatch_conversions = ~0u;

	for(i = 0; i < max && isn->constraints[i].category[0]; i++){
		bool matches[MAX_OPERANDS];
		unsigned nmatches = 0;
		unsigned conversions_required;
		unsigned conversions_left;
		unsigned j;

		/* how many conversions for this constrant-set? */
		for(j = 0; j < nargs; j++){
			if(j == isn->arg_count)
				break;

			matches[j] = (arg_cats[j] == isn->constraints[i].category[j]);

			if(matches[j])
				nmatches++;
		}

		conversions_required = (nargs - nmatches);

		if(conversions_required == 0){
			bestmatch_conversions = conversions_required;
			bestmatch_i = i;
			break;
		}

		/* we can only have the best match if the non-matched operand
		 * is convertible to the required operand type */
		conversions_left = conversions_required;
		for(j = 0; j < nargs; j++){
			if(matches[j])
				continue;

			if(operand_type_convertible(
						arg_cats[j], isn->constraints[i].category[j]))
			{
				conversions_left--;
			}
		}

		/* if we can convert all operands, we may have a new best match */
		if(conversions_left == 0 /* feasible */
		&& conversions_required < bestmatch_conversions /* best */)
		{
			bestmatch_conversions = conversions_required;
			bestmatch_i = i;
		}
	}

	*out_conversions_required = bestmatch_conversions;

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
			*deref_val,
			octx);

	/* if we're handling a non-lvalue (i.e. array, struct [alloca])
	 * we actually want its address*/
	if(!*deref_val && must_lea_val(orig_val)){
	}
	if(TEMPORARY_SHOW_MOVES){
		x86_comment(octx, "XXX: next mov should be lea? deref=%d must_lea()=%d",
				*deref_val, must_lea_val(orig_val)
				);
	}

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
			*deref_val,
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
	unsigned conversions_required, conversions_left;
	const struct x86_isn_constraint *operands_target = NULL;
	unsigned j;

	assert(operand_count == isn->arg_count);
	assert(operand_count <= MAX_OPERANDS);

	for(j = 0; j < operand_count; j++){
		emit_vals[j] = operands[j].val;
		orig_dereference[j] = operands[j].dereference;

		op_categories[j] = val_category(operands[j].val, operands[j].dereference);
	}

	operands_target = find_isn_bestmatch(
			isn, op_categories, operand_count, &conversions_required);

	if(!operands_target)
		return false;

	/* not satisfied - convert an operand to REG or MEM */
	if(conversions_required && TEMPORARY_SHOW_MOVES)
		x86_comment(octx, "temp(s) needed for %s (%u conversions)",
				isn->mnemonic, conversions_required);

	conversions_left = conversions_required;
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
			conversions_left--;
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
			conversions_left--;
		}
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

			conversions_left--;
		}
	}

	assert(conversions_left == 0 && "should have converted all operands");
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

static void mov_deref_force(
		val *from, val *to,
		x86_octx *octx,
		bool deref_from, bool deref_to,
		bool const force)
{
	const struct x86_isn *chosen_isn = &x86_isn_mov;

	if(!force && !deref_from && !deref_to){
		struct location *loc_from, *loc_to;

		loc_from = val_location(from);
		loc_to = val_location(to);

		if(loc_from && loc_to
		&& loc_from->where == NAME_IN_REG
		&& loc_to->where == NAME_IN_REG
		&& loc_from->u.reg == loc_to->u.reg) /* FIXME: regt_equal */
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
			struct location *loc;

		case LITERAL:
			break;

		case ARGUMENT:
			loc = &lval->u.argument.loc;
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
		case ABI_TEMP:
			loc = &lval->u.abi;
			goto loc;

loc:
		{
			long offset;
			if(!elem_get_offset(i->u.elem.index, val_type(i->u.elem.res), &offset))
				break;

			switch(loc->where){
				case NAME_NOWHERE:
					assert(0 && "NAME_NOWHERE in backend");

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
				{
					char regch = x86_target_regch(unit_target_info(octx->unit));

					fprintf(octx->fout, "\tlea %ld(%%%cbp), %s\n",
							-(long)loc->u.off + offset,
							regch,
							x86_val_str(
								i->u.elem.res,
								0,
								octx,
								val_type(i->u.elem.res),
								DEREFERENCE_FALSE));
					return;
				}
			}
			break;
		}

		case GLOBAL:
		{
			const char *result_str;
			long offset;
			int x64;

			if(!elem_get_offset(i->u.elem.index, val_type(i->u.elem.res), &offset))
				break;

			x64 = x86_target_switch(unit_target_info(octx->unit), 0, 1);

			result_str = x86_val_str(
					i->u.elem.res, 1, octx,
					val_type(i->u.elem.res),
					DEREFERENCE_FALSE);

			/* TODO: pic */
			fprintf(octx->fout, "\tlea %s+%ld%s, %s\n",
					global_name(lval->u.global),
					offset,
					x64 ? "(%rip)" : "",
					result_str);
			return;
		}
	}

	assert(0 && "TODO: isel-level pointer arithmetic");
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
		case op_add: break;

		case op_sub:          opisn.mnemonic = "sub"; break;
		case op_mul:          opisn.mnemonic = "imul"; break;
		case op_and:          opisn.mnemonic = "and"; break;
		case op_or:           opisn.mnemonic = "or"; break;
		case op_xor:          opisn.mnemonic = "xor"; break;

		case op_shiftl:       opisn.mnemonic = "shl"; break;
		case op_shiftr_arith: opisn.mnemonic = "sar"; break;
		case op_shiftr_logic: opisn.mnemonic = "shr"; break;

		case op_sdiv:
		case op_smod:
		case op_udiv:
		case op_umod:
		{
			emit_isn_operand op;
			op.val = rhs;
			op.dereference = false;
			opisn.mnemonic = "idiv";
			opisn.arg_count = 1; /* idiv takes an implicit second operand */
			x86_emit_isn(&opisn, octx, &op, 1, NULL);
			return;
		}
	}

	x86_mov(lhs, res, octx);
	emit_isn_binary(&opisn, octx, rhs, false, res, false, NULL);
}

static void x86_trunc(val *from, val *to, x86_octx *octx)
{
	/* temporarily mutate from - note this only works for little endian */
	type *const from_type = from->ty;

	from->ty = to->ty;

	x86_comment(octx, "trunc:");
	x86_mov(from, to, octx);

	from->ty = from_type;
}

static void x86_ext_reg(
		val *from,
		val *to,
		x86_octx *octx,
		bool sign,
		unsigned sz_from,
		unsigned sz_to)
{
	char buf[4] = { 0 };
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

static void x86_ext(val *from, val *to, const bool sign, x86_octx *octx)
{
	unsigned sz_from = val_size(from);
	unsigned sz_to = val_size(to);
	struct location *from_loc = val_location(from);
	struct location *to_loc = val_location(to);

	if(sz_from > sz_to){
		/* trunc */
		return x86_trunc(from, to, octx);
	}

	/* zext requires something in a reg */
	if(from_loc && from_loc->where == NAME_IN_REG
	&& to_loc   && to_loc->where == NAME_IN_REG)
	{
		x86_ext_reg(from, to, octx, sign, sz_from, sz_to);
	}
	else
	{
		val short_reg, long_reg;

		x86_make_reg(&short_reg, SCRATCH_REG, val_type(from));
		x86_make_reg(&long_reg,  SCRATCH_REG, val_type(to));

		x86_mov(from, &short_reg, octx);

		x86_ext_reg(&short_reg, &long_reg, octx, sign, sz_from, sz_to);

		x86_mov(&long_reg, to, octx);
	}
}

static void x86_cmp(
		enum op_cmp cmp,
		val *lhs, val *rhs, val *res,
		x86_octx *octx)
{
	val *zero;
	emit_isn_operand set_operand;

	x86_comment(octx, "PRE-BINARY CMP");
	emit_isn_binary(&x86_isn_cmp, octx,
			/* x86 cmp operands are reversed */
			rhs, false,
			lhs, false,
			NULL);
	x86_comment(octx, "POST-BINARY CMP");

	zero = val_retain(val_new_i(0, res->ty));

	x86_mov(zero, res, octx);

	set_operand.val = res;
	set_operand.dereference = false;

	x86_emit_isn(&x86_isn_set, octx, &set_operand, 1, x86_cmp_str(cmp));

	val_release(zero);
}

static void x86_jmp(x86_octx *octx, block *target)
{
	fprintf(octx->fout, "\tjmp %s\n", target->lbl);
}

static void x86_branch(val *cond, block *bt, block *bf, x86_octx *octx)
{
	emit_isn_binary(&x86_isn_test, octx,
			cond, false,
			cond, false,
			NULL);

	fprintf(octx->fout, "\tjz %s\n", bf->lbl);

	x86_jmp(octx, bt);
}

static void x86_block_enter(x86_octx *octx, block *blk)
{
	if(!blk->lbl)
		return;

	fprintf(octx->fout, "%s:\n", blk->lbl);
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

			case ISN_IMPLICIT_USE:
				break;

			case ISN_RET:
			{
				fprintf(octx->fout, "\tjmp %s\n", octx->exitblk->lbl);
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
			case ISN_PTRSUB:
				x86_op(
						i->type == ISN_PTRADD ? op_add : op_sub,
						i->u.ptraddsub.lhs,
						i->u.ptraddsub.rhs,
						i->u.ptraddsub.out,
						octx);
				break;

			case ISN_OP:
				x86_op(i->u.op.op, i->u.op.lhs, i->u.op.rhs, i->u.op.res, octx);
				break;

			case ISN_CMP:
				x86_cmp(i->u.cmp.cmp,
						i->u.cmp.lhs, i->u.cmp.rhs, i->u.cmp.res,
						octx);
				break;

			case ISN_EXT_TRUNC:
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
						i->u.call.into,
						i->u.call.fn,
						&i->u.call.args,
						octx);
				break;
			}
		}
	}
}

static void x86_emit_epilogue(x86_octx *octx, block *exit)
{
	x86_block_enter(octx, exit);
	fprintf(octx->fout, "\tleave\n" "\tret\n");
}

static void x86_emit_prologue(
		function *func,
		long alloca_total,
		unsigned align,
		const struct target *target)
{
	const char *fname;
	char regch = x86_target_regch(target);

	printf(".text\n");
	fname = function_name(func);
	printf(".globl %s\n", fname);
	printf("%s:\n", fname);

	printf("\tpush %%%cbp\n"
			"\tmov %%%csp, %%%cbp\n",
			regch, regch, regch);

	if(align){
		alloca_total = (alloca_total + align) & ~(align - 1);
	}

	if(alloca_total)
		printf("\tsub $%ld, %%%csp\n", alloca_total, regch);
}

static long x86_alloca_total(x86_octx *octx)
{
	return octx->alloca_bottom + octx->spill_alloca_max;
}

static void x86_out_fn(unit *unit, function *func)
{
	unsigned alloca_sum = 0;
	x86_octx out_ctx = { 0 };
	block *const entry = function_entry_block(func, false);
	block *const exit = function_exit_block(func, unit);
	dynmap *markers = BLOCK_DYNMAP_NEW();

	out_ctx.unit = unit;

	out_ctx.fout = tmpfile();
	if(!out_ctx.fout)
		die("tmpfile():");

	out_ctx.exitblk = exit;
	out_ctx.func = func;

	/* start at the bottom of allocas */
	out_ctx.alloca_bottom = alloca_sum;

	blocks_traverse(entry, x86_out_block1, &out_ctx, markers);
	x86_emit_epilogue(&out_ctx, exit);

	/* now we spit out the prologue first */
	x86_emit_prologue(
			func,
			x86_alloca_total(&out_ctx),
			out_ctx.max_align,
			unit_target_info(unit));

	if(cat_file(out_ctx.fout, stdout) != 0)
		die("cat file:");

	if(fclose(out_ctx.fout))
		die("close:");

	dynmap_free(markers);
}

static void x86_emit_space(unsigned space)
{
	if(space)
		printf(".space %u\n", space);
}

static void x86_out_padding(size_t *const bytes, unsigned align)
{
	unsigned gap = gap_for_alignment(*bytes, align);

	if(gap == 0)
		return;

	x86_emit_space(gap);
	*bytes += gap;
}

static void x86_out_init(struct init *init, type *ty)
{
	switch(init->type){
		case init_int:
			printf(".%s %#llx\n",
					x86_size_name(type_size(ty)),
					init->u.i);
			break;

		case init_alias:
		{
			const unsigned tsize = type_size(ty);
			const unsigned as_size = type_size(init->u.alias.as);
			char buf[256];

			assert(as_size <= tsize);

			printf(X86_COMMENT_STR " alias init %s as %s:\n",
					type_to_str_r(buf, sizeof(buf), ty),
					type_to_str(init->u.alias.as));

			x86_out_init(init->u.alias.init, init->u.alias.as);

			x86_emit_space(tsize - as_size);
			break;
		}

		case init_str:
		{
			printf(".ascii \"");
			dump_escaped_string(&init->u.str);
			printf("\"\n");
			break;
		}

		case init_array:
		{
			size_t i;
			type *elemty = type_array_element(ty);

			dynarray_iter(&init->u.elem_inits, i){
				struct init *elem = dynarray_ent(&init->u.elem_inits, i);

				x86_out_init(elem, elemty);
			}
			break;
		}

		case init_struct:
		{
			size_t i;
			size_t bytes = 0;

			dynarray_iter(&init->u.elem_inits, i){
				struct init *elem = dynarray_ent(&init->u.elem_inits, i);
				type *elemty = type_struct_element(ty, i);
				struct
				{
					unsigned size;
					unsigned align;
				} attr;

				type_size_align(elemty, &attr.size, &attr.align);

				x86_out_padding(&bytes, attr.align);

				x86_out_init(elem, elemty);

				bytes += attr.size;
			}

			x86_out_padding(&bytes, type_align(ty));
			break;
		}

		case init_ptr:
		{
			printf(".%s ", x86_size_name(type_size(ty)));

			if(init->u.ptr.is_label){
				long off = init->u.ptr.u.ident.label.offset;

				printf("%s %s %ld",
						init->u.ptr.u.ident.label.ident,
						off > 0 ? "+" : "-",
						off > 0 ? off : -off);
			}else{
				printf("%lu", init->u.ptr.u.integral);
			}
			putchar('\n');
			break;
		}
	}
}

static void x86_out_align(unsigned align, const struct target *target)
{
	if(target->sys.align_is_pow2)
		align = log2i(align);

	printf(".align %u\n", align);
}

static void x86_out_var(variable_global *var, const struct target *target_info)
{
	variable *inner = variable_global_var(var);
	const char *name = variable_name(inner);
	struct init_toplvl *init_top = variable_global_init(var);
	type *var_ty = variable_type(inner);

	/* TODO: use .bss for zero-init */

	if(init_top && init_top->constant){
		printf("%s\n", target_info->sys.section_rodata);

	}else{
		printf(".data\n");
	}

	if(init_top){
		if(!init_top->internal)
			printf(".globl %s\n", name);

		if(init_top->weak)
			printf("%s %s\n", target_info->sys.weak_directive_var, name);
	}

	x86_out_align(type_align(var_ty), target_info);

	printf("%s:\n", name);

	if(init_top){
		x86_out_init(init_top->init, var_ty);
	}else{
		printf(".space %u\n", variable_size(inner));
	}
}

void x86_out(unit *unit, global *glob)
{
	const struct target *target = unit_target_info(unit);

	if(global_is_forward_decl(glob)){
		x86_octx octx = { 0 };
		octx.fout = stdout;
		x86_comment(&octx, "forward decl %s", global_name(glob));

		if(glob->kind == GLOBAL_FUNC
		&& function_attributes(glob->u.fn) & function_attribute_weak)
		{
			printf("%s %s\n", target->sys.weak_directive_func, global_name(glob));
		}
		return;
	}

	switch(glob->kind){
		case GLOBAL_FUNC:
		{
			function *fn = glob->u.fn;

			/* should have entry block, otherwise it's a forward decl */
			assert(function_entry_block(fn, false));
			x86_out_fn(unit, fn);

			break;
		}

		case GLOBAL_VAR:
			x86_out_var(glob->u.var, target);
			break;

		case GLOBAL_TYPE:
			break;
	}
}
