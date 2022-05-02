use std::collections::HashSet;
use std::io::{self, Write};

use super::{Abi, Target};
use crate::block::PBlock;
use crate::func::{Func, FuncAttr};
use crate::init::InitTopLevel;
use crate::isn::Isn;
use crate::size_align::Align;
use crate::ty_uniq::TyUniq;
use crate::variable::Var;
use crate::{global::Global, regset::RegSet};
// use reg::{Class, Reg};

pub static ABI: Abi = Abi {
    scratch_regs: RegSet {},
    /*
    regt_make(0, 0), /* eax */
    regt_make(2, 0), /* ecx */
    regt_make(3, 0), /* edx */
    regt_make(4, 0), /* esi */
    regt_make(5, 0)  /* edi */
    */
    ret_regs: RegSet {},
    /*
    regt_make(0, 0), /* rax */
    regt_make(3, 0)  /* rdx */
    */
    arg_regs: RegSet {},
    /*
    regt_make(4, 0), /* rdi */
    regt_make(5, 0), /* rsi */
    regt_make(3, 0), /* rdx */
    regt_make(2, 0)  /* rcx */
    // TODO: r8, r9
    */
    callee_saves: RegSet {},
    /*
    regt_make(1, 0) /* rbx */
    */
};

type Result = io::Result<()>;

struct X86<'a, 'arena> {
    target: &'a Target,
    types: &'a TyUniq<'arena>,
    out: &'a mut dyn Write,
}

struct X86PerFunc<'a, 'b, 'arena> {
    x86: &'b mut X86<'a, 'arena>,

    out: &'b mut dyn Write,

    func: &'b Func<'arena>,
    exitblk: PBlock<'arena>,
    emitted: HashSet<PBlock<'arena>>,

    stack: Stack,
    max_align: Option<Align>,
    scratch_reg_reserved: bool,
}

struct Stack {
    current: u32,
    call_spill_max: u32,
}

pub fn emit<'arena>(
    g: &mut Global<'arena>,
    target: &Target,
    types: &TyUniq<'arena>,
    out: &mut dyn Write,
) -> Result {
    let mut x86 = X86 { target, types, out };

    match g {
        Global::Func(f) => {
            /* should have entry block, otherwise it's a forward decl */
            match f.entry() {
                Some(_) => x86.emit_func(f),
                None => {
                    x86.comment(format!("forward decl {}", f.name.mangled(target)))?;

                    if f.attr().contains(FuncAttr::WEAK) {
                        write!(
                            out,
                            "{} {}",
                            target.sys.weak_directive_func,
                            f.name.mangled(target)
                        )?;
                    }

                    Ok(())
                }
            }
        }
        Global::Var(v) => match &v.init {
            Some(init) => x86.emit_var(v, init),
            None => x86.comment(format!("forward decl {}", v.name.mangled(target))),
        },
        Global::Type { .. } => Ok(()),
    }
}

impl<'a, 'arena> X86<'a, 'arena> {
    fn emit_func(&mut self, func: &mut Func<'arena>) -> Result {
        let exit = func.exit_block();

        let mut prologue = Vec::new();
        let mut body = Vec::new();

        let mut per_func = X86PerFunc {
            x86: self,
            out: &mut body,

            func,
            exitblk: exit,
            emitted: HashSet::new(),

            stack: Stack {
                current: func.get_stack_use(),
                call_spill_max: 0,
            },
            max_align: None,
            scratch_reg_reserved: false,
        };

        per_func.emit()?;

        /* now we spit out the prologue first */
        per_func.out = &mut prologue;
        per_func.emit_prologue()?;

        self.out.write_all(&prologue)?;
        self.out.write_all(&body)?;

        Ok(())
    }

    fn emit_var(&self, _v: &Var, _init: &InitTopLevel) -> Result {
        todo!()
    }

    fn comment(&self, _comment: String) -> Result {
        todo!()
    }
}

impl<'a, 'b, 'arena> X86PerFunc<'a, 'b, 'arena> {
    fn emit(&mut self) -> Result {
        for b in self.func.blocks() {
            self.emit_block1(b)?;
        }
        self.emit_epilogue()?;

        Ok(())
    }

    fn emit_epilogue(&mut self) -> Result {
        self.block_enter(self.exitblk)?;
        write!(self.out, "\tleave\n\tret\n")
    }

    fn block_enter(&mut self, b: PBlock<'arena>) -> Result {
        if let Some(l) = &*b.label() {
            write!(self.out, "{}:\n", l)?;
        }
        Ok(())
    }

    fn emit_prologue(&mut self) -> Result {
        write!(self.out, ".text\n")?;
        let fname = self.func.name.mangled(self.x86.target);

        if !self.func.attr().contains(FuncAttr::INTERNAL) {
            write!(self.out, ".globl {}\n", fname)?;
        }
        write!(self.out, "{}:\n", fname)?;

        let regch = self.regch();
        write!(
            self.out,
            "\tpush %{}bp\n\tmov %{}sp, %{}bp\n",
            regch, regch, regch
        )?;

        let mut alloca_total = self.stack.current + self.stack.call_spill_max;
        if let Some(align) = self.max_align {
            alloca_total = (alloca_total + align.get()) & !(align.get() - 1);
        }

        if alloca_total != 0 {
            write!(self.out, "\tsub ${}, %{}sp\n", alloca_total, regch)?;
        }

        Ok(())
    }

    fn regch(&self) -> char {
        match self.x86.target.arch.ptr.size {
            4 => 'e',
            8 => 'r',
            _ => unreachable!(),
        }
    }

    fn emit_block1(&mut self, b: PBlock<'arena>) -> Result {
        if !self.emitted.insert(b) {
            return Ok(());
        }

        self.block_enter(b)?;

        for isn in &*b.isns() {
            use Isn::*;
            match isn {
                Jmp(dest) => self.jmp(dest),
                Ret(_v) => {
                    write!(
                        self.out,
                        "\tjmp {}\n",
                        self.exitblk.label().as_ref().unwrap()
                    )
                } /*
                  case ISN_ALLOCA:
                      break;

                  case ISN_IMPLICIT_USE_START:
                  case ISN_IMPLICIT_USE_END:
                      break;

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

                  case ISN_MEMCPY:
                      break;

                  case ISN_JMP_COMPUTED:
                  {
                      x86_jmp_comp(octx, i->u.jmpcomp.target);
                      break;
                  }

                  case ISN_LABEL:
                      break;

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
                      x86_emit_call(blk, i,
                          i->u.call.into,
                          i->u.call.fn,
                          &i->u.call.args,
                          octx);
                      break;
                  }

                  case ISN_ASM:
                      write!(self.out, "\t");
                      fwrite(i->u.as.str, sizeof(i->u.as.str[0]), i->u.as.len, octx->fout);
                      write!(self.out, "\n");
                      break;
                          */
            }?;
        }

        Ok(())
    }

    fn jmp(&mut self, target: PBlock<'_>) -> Result {
        write!(self.out, "\tjmp {}\n", target.label().as_ref().unwrap())
    }
}

/*
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
#include "backend_isn.h"

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
#include "x86_isn.h"

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

static enum operand_category val_category(val *v, bool dereference)
{
    /* FIXME: same as val_operand_category */
    switch(v->kind){
        case LITERAL:
            return dereference ? OPERAND_MEM_CONTENTS : OPERAND_INT;

        case LABEL:
        case GLOBAL:
        case ALLOCA:
            return dereference ? OPERAND_MEM_CONTENTS : OPERAND_MEM_PTR;

        case UNDEF:
        case LOCAL:
            return OPERAND_REG;
    }
    assert(0);
}

static bool must_lea_val(val *v)
{
    /* this assumes we haven't been told to dereference */

    switch(v->kind){
        case ALLOCA:
        case GLOBAL:
        case LABEL:
            return true;

        case UNDEF:
        case LITERAL:
        case LOCAL:
            return false;
    }
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

    if(!regt_is_valid(reg)){
        return "<invalid reg>";
    }

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

    write!(self.out, "\t" X86_COMMENT_STR " ");

    va_start(l, fmt);
    vwrite!(self.out, fmt, l);
    va_end(l);

    write!(self.out, "\n");
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
        case LABEL: return false;
        case UNDEF: return false;

        case LOCAL:
            loc = &val->u.local.loc;
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
        case NAME_IN_REG_ANY:
            assert(0 && "NAME_NOWHERE/NAME_IN_REG_ANY in backend");

        case NAME_IN_REG:
        {
            const bool deref = (dereference_ty == DEREFERENCE_TRUE);
            int val_sz;

            if(deref){
                val_sz = target->arch.ptr.size;
            }else{
                val_sz = type_size(ty);
            }

            xsnprintf(buf, bufsz, "%s%%%s%s",
                    deref ? "(" : "",
                    name_in_reg_str(loc, val_sz),
                    deref ? ")" : "");
            break;
        }

        case NAME_SPILT:
        {
            char regch = x86_target_regch(target);

            xsnprintf(buf, bufsz, "-%u(%%%cbp)%s", loc->u.off, regch,
                    dereference_ty == DEREFERENCE_TRUE ? "" : "/* expected to deref */
");
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

			xsnprintf(buf, sizeof bufs[0],
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
			xsnprintf(buf, sizeof bufs[0],
					"%s%s%s",
					indir ? "" : "$",
					global_name_mangled(val->u.global, unit_target_info(octx->unit)),
					x64 ? "(%rip)" : "");
			break;
		}

		case LABEL:
		{
			const struct target *target = unit_target_info(octx->unit);
			const int x64 = x86_target_switch(target, 0, 1);
			const char *prefix = unit_lbl_private_prefix(octx->unit);

			xsnprintf(buf, sizeof bufs[0], "%s_%s%s", prefix, val->u.label.name, x64 ? "(%rip)" : "");
			break;
		}

		case ALLOCA: loc = &val->u.alloca.loc; goto loc;
		case LOCAL: loc = &val->u.local.loc; goto loc;
loc:
		{
			return x86_name_str(
					loc,
					buf, sizeof bufs[0],
					dereference,
					operand_output_ty,
					unit_target_info(octx->unit));
		}

		case UNDEF:
xsnprintf(buf, sizeof bufs[0], "%%eax /*undef*/
");
			break;
	}

	return buf;
}

#if 0
static const char *x86_val_str_debug(val *v, int bufidx, x86_octx *octx)
{
	return x86_val_str(v, bufidx, octx, val_type(v), DEREFERENCE_ANY);
}
#endif

void x86_make_stack_slot(val *stack_slot, unsigned off, type *ty)
{
	val_temporary_init(stack_slot, ty);

	stack_slot->u.local.loc.where = NAME_SPILT;
	stack_slot->u.local.loc.u.off = off;
}

void x86_make_reg(val *reg, int regidx, type *ty)
{
	val_temporary_init(reg, ty);

	reg->u.local.loc.where = NAME_IN_REG;
	reg->u.local.loc.u.reg = regidx; /* FIXME: regt indexing */
}

void x86_make_eax(val *out, type *ty)
{
	x86_make_reg(out, /* XXX: hard coded eax: */ 0, ty);
}

#if 0
static void make_val_temporary_store(
		val *from,
		val *write_to,
		enum operand_category from_cat,
		enum operand_category to_cat,
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
#endif

#if 0
static void ready_input(
		val *orig_val,
		val *temporary_store,
		enum operand_category orig_val_category,
		enum operand_category operand_category,
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
		enum operand_category orig_val_category,
		enum operand_category operand_category,
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
#endif

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
		const struct backend_isn *isn, x86_octx *octx,
		emit_isn_operand operands[],
		unsigned operand_count,
		const char *x86_isn_suffix)
{
	val *emit_vals[MAX_OPERANDS];
	bool orig_dereference[MAX_OPERANDS];
	enum operand_category op_categories[MAX_OPERANDS] = { 0 };
#if 0
	struct {
		val val;
		variable var;
	} temporaries[MAX_OPERANDS];
#endif
	unsigned conversions_required = 0, conversions_left;
#if 0
	const struct x86_isn_constraint *operands_target = NULL;
#endif
	unsigned j;

	/*assert(operand_count == isn->operand_count);*/
	assert(operand_count <= MAX_OPERANDS);

	for(j = 0; j < operand_count; j++){
		emit_vals[j] = operands[j].val;
		orig_dereference[j] = operands[j].dereference;

		op_categories[j] = val_category(operands[j].val, operands[j].dereference);
	}

#if 0
	operands_target = find_isn_bestmatch(
			isn, op_categories, operand_count, &conversions_required);

	if(!operands_target)
		return false;
#endif

	/* not satisfied - convert an operand to REG or MEM */
	if(conversions_required && TEMPORARY_SHOW_MOVES)
		x86_comment(octx, "temp(s) needed for %s (%u conversions)",
				isn->mnemonic, conversions_required);

	if(conversions_required){
		fprintf(stderr, "conversions required, too late - should've been done in isel\n");
		fprintf(stderr, "  isn: %s\n", isn->mnemonic);
		fprintf(stderr, "  operands:\n");
		for(j = 0; j < operand_count; j++){
			fprintf(
					stderr,
					"    %s%s\n",
					val_str(operands[j].val),
					/*op_categories[j] == operands_target->category[j]*/0 ? "" : " (conversion required)");
		}
		abort();
	}

	conversions_left = conversions_required;
#if 0
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
#endif

	if(!x86_isn_suffix && isn->may_suffix)
		x86_isn_suffix = maybe_generate_isn_suffix(operand_count, emit_vals, operands);
	if(!x86_isn_suffix)
		x86_isn_suffix = "";

	write!(self.out, "\t%s%s ", isn->mnemonic, x86_isn_suffix);

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
			write!(self.out, "{%s}", type_to_str(emit_vals[j]->ty));

		fprintf(octx->fout, "%s%s",
				val_str,
				j + 1 == operand_count ? "\n" : ", ");
	}


#if 0
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
#endif

	assert(conversions_left == 0 && "should have converted all operands");
	return true;
}

void x86_emit_isn(
		const struct backend_isn *isn, x86_octx *octx,
		emit_isn_operand operands[],
		unsigned operand_count,
		const char *x86_isn_suffix)
{
	bool did = emit_isn_try(isn, octx, operands, operand_count, x86_isn_suffix);

	assert(did && "couldn't satisfy operands for isn");
}

static void emit_isn_binary(
		const struct backend_isn *isn, x86_octx *octx,
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
	const struct backend_isn *chosen_isn = &x86_isn_mov;

	if(!force && !deref_from && !deref_to){
		struct location *loc_from, *loc_to;

		loc_from = val_location(from);
		loc_to = val_location(to);

		if(loc_from && loc_to
		&& loc_from->where == NAME_IN_REG
		&& loc_to->where == NAME_IN_REG
		&& loc_from->u.reg == loc_to->u.reg) /* FIXME: regt_equal */
		{
			write!(self.out, "\t#");
		}
	}

	/* if we're x86_mov:ing from a non-lvalue (i.e. array, struct [alloca])
	 * we actually want its address*/
	if(!deref_from && must_lea_val(from)){
		chosen_isn = &x86_isn_lea;
		deref_from = true;
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

static void x86_op(
		enum op op, val *lhs, val *rhs,
		val *res, x86_octx *octx)
{
	bool deref_lhs, deref_rhs, deref_res;
	struct backend_isn opisn;

	if(op == op_mul && /* XXX: disabled */false){
		/* use the three-operand mode */
		emit_isn_operand operands[3] = { 0 };

		/* XXX: Note for re-enabling this: ops are all done backwards (for sub) */
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

			x86_emit_isn(
					&opisn,
					octx,
					&op,
					1, /* idiv takes an implicit second operand */
					NULL);
			return;
		}
	}

	deref_lhs = val_on_stack(lhs);
	deref_rhs = val_on_stack(rhs);
	deref_res = val_on_stack(res);

	/* to match isel's selections (or more specifically, how we define backend
	 * instructions in x86_isns.c as l=INPUT r=INPUT|OUTPUT), we must match the
	 * rhs and result operands to ensure that we do `op <lhs>, <rhs/res>`
	 *
	 * except to preserve things like `a - b`, we must ensure the right hand side
	 * operand is `a`. hence why we move `lhs` into `res` here, and hence the
	 * changed corresponding isn definitions.
	 */
	x86_mov_deref(lhs, res, octx, deref_lhs, false);
	emit_isn_binary(&opisn, octx, rhs, deref_rhs, res, deref_res, NULL);
}

static void emit_elem(isn *i, x86_octx *octx)
{
	val *const lval = i->u.elem.lval;
	long offset = -1;
	type *maybe_suty = type_deref(val_type(lval));


	if(!type_array_element(maybe_suty)){
		assert(type_is_struct(maybe_suty));
		assert(i->u.elem.index->kind == LITERAL);

		offset = type_struct_offset(maybe_suty, i->u.elem.index->u.i);
	}

	if(offset >= 0 || elem_get_offset(i->u.elem.index, val_type(i->u.elem.res), &offset)){
		switch(lval->kind){
				struct location *loc;

			case UNDEF:
				break;

			case LITERAL:
				break;

			case ALLOCA:
				loc = &lval->u.alloca.loc;
				goto loc;
			case LOCAL:
				loc = &lval->u.local.loc;
				goto loc;
loc:
				{
					switch(loc->where){
						case NAME_NOWHERE:
						case NAME_IN_REG_ANY:
							fprintf(stderr, "%s loc { %#x, %#x }\n",
									val_str(lval),
									loc->where,
									loc->u.reg);
							assert(0 && "NAME_NOWHERE/NAME_IN_REG_ANY in backend XXX");

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
				int x64;

				x64 = x86_target_switch(unit_target_info(octx->unit), 0, 1);

				result_str = x86_val_str(
						i->u.elem.res, 1, octx,
						val_type(i->u.elem.res),
						DEREFERENCE_FALSE);

				/* TODO: pic */
				fprintf(octx->fout, "\tlea %s+%ld%s, %s\n",
						global_name_mangled(lval->u.global, unit_target_info(octx->unit)),
						offset,
						x64 ? "(%rip)" : "",
						result_str);
				return;
			}

			case LABEL:
				assert(0 && "unreachable");
		}
	}

	assert(type_array_element(type_deref(val_type(lval))));
	/* do the equivalent of x86_op/add,
	 * but with more control over deref_[lr]hs and lea'ing of the lval */
	x86_mov(lval, i->u.elem.res, octx);
	emit_isn_binary(&x86_isn_add, octx, i->u.elem.index, false, i->u.elem.res, false, NULL);
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

	if(sz_from > sz_to){
		/* trunc */
		return x86_trunc(from, to, octx);
	}

	x86_ext_reg(from, to, octx, sign, sz_from, sz_to);
}

static void x86_cmp(
		enum op_cmp cmp,
		val *lhs, val *rhs, val *res,
		x86_octx *octx)
{
	val *zero;
	emit_isn_operand set_operand;

	emit_isn_binary(&x86_isn_cmp, octx,
			/* x86 cmp operands are reversed */
			rhs, false,
			lhs, false,
			NULL);

	zero = val_retain(val_new_i(0, res->ty));

	x86_mov(zero, res, octx);

	set_operand.val = res;
	set_operand.dereference = false;

	x86_emit_isn(&x86_isn_set, octx, &set_operand, 1, x86_cmp_str(cmp));

	val_release(zero);
}

static void x86_jmp_comp(x86_octx *octx, val *target)
{
	write!(self.out, "\tjmp *%s\n", x86_val_str(target, 0, octx, val_type(target), DEREFERENCE_FALSE));
}

static void x86_branch(val *cond, block *bt, block *bf, x86_octx *octx)
{
	if(val_is_mem(cond)){
		val zero;

		val_temporary_init(&zero, val_type(cond));
		zero.kind = LITERAL;
		zero.u.i = 0;

		emit_isn_binary(&x86_isn_cmp, octx,
				&zero, false,
				cond, false,
				NULL);
	}else{
		emit_isn_binary(&x86_isn_test, octx,
				cond, false,
				cond, false,
				NULL);
	}

	write!(self.out, "\tjz %s\n", bf->lbl);

	x86_jmp(octx, bt);
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

static void x86_emit_space(unsigned space, x86_octx *octx)
{
	if(space)
		write!(self.out, ".space %u\n", space);
}

static void x86_out_padding(size_t *const bytes, unsigned align, x86_octx *octx)
{
	unsigned gap = gap_for_alignment(*bytes, align);

	if(gap == 0)
		return;

	x86_emit_space(gap, octx);
	*bytes += gap;
}

static void x86_out_init(struct init *init, type *ty, x86_octx *octx)
{
	switch(init->type){
		case init_int:
			fprintf(octx->fout, ".%s %#llx\n",
					x86_size_name(type_size(ty)),
					init->u.i);
			break;

		case init_alias:
		{
			const unsigned tsize = type_size(ty);
			const unsigned as_size = type_size(init->u.alias.as);
			char buf[256];
			struct init *sub;

			assert(as_size <= tsize);

			fprintf(octx->fout, X86_COMMENT_STR " alias init %s as %s:\n",
					type_to_str_r(buf, sizeof(buf), ty),
					type_to_str(init->u.alias.as));

			sub = init->u.alias.init;
			/* make zeroinits look nice */
			if(sub->type == init_int && sub->u.i == 0){
				x86_emit_space(tsize, octx);
			}else{
				x86_out_init(init->u.alias.init, init->u.alias.as, octx);
				x86_emit_space(tsize - as_size, octx);
			}
			break;
		}

		case init_str:
		{
			write!(self.out, ".ascii \"");
			dump_escaped_string(&init->u.str, octx->fout);
			write!(self.out, "\"\n");
			break;
		}

		case init_array:
		{
			size_t i;
			type *elemty = type_array_element(ty);

			dynarray_iter(&init->u.elem_inits, i){
				struct init *elem = dynarray_ent(&init->u.elem_inits, i);

				x86_out_init(elem, elemty, octx);
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

				x86_out_padding(&bytes, attr.align, octx);

				x86_out_init(elem, elemty, octx);

				bytes += attr.size;
			}

			x86_out_padding(&bytes, type_align(ty), octx);
			break;
		}

		case init_ptr:
		{
			write!(self.out, ".%s ", x86_size_name(type_size(ty)));

			if(init->u.ptr.is_label){
				long off = init->u.ptr.u.ident.label.offset;

				fprintf(octx->fout, "%s %s %ld",
						init->u.ptr.u.ident.label.ident,
						off > 0 ? "+" : "-",
						off > 0 ? off : -off);
			}else{
				write!(self.out, "%lu", init->u.ptr.u.integral);
			}
			fputc('\n', octx->fout);
			break;
		}
	}
}

static void x86_out_align(unsigned align, const struct target *target, x86_octx *octx)
{
	if(target->sys.align_is_pow2)
		align = log2i(align);

	write!(self.out, ".align %u\n", align);
}

static void x86_out_var(variable_global *var, const struct target *target_info, x86_octx *octx)
{
	variable *inner = variable_global_var(var);
	const char *name = variable_name_mangled(inner, unit_target_info(octx->unit));
	struct init_toplvl *init_top = variable_global_init(var);
	type *var_ty = variable_type(inner);

/* TODO: use .bss for zero-init */

	if(init_top && init_top->constant){
		write!(self.out, "%s\n", target_info->sys.section_rodata);

	}else{
		write!(self.out, ".data\n");
	}

	if(init_top){
		if(!init_top->internal)
			write!(self.out, ".globl %s\n", name);

		if(init_top->weak)
			write!(self.out, "%s %s\n", target_info->sys.weak_directive_var, name);
	}

	x86_out_align(type_align(var_ty), target_info, octx);

	write!(self.out, "%s:\n", name);

	if(init_top){
		x86_out_init(init_top->init, var_ty, octx);
	}else{
		write!(self.out, ".space %u\n", variable_size(inner));
	}
}
*/
