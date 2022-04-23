use std::{collections::HashMap, rc::Rc};

use bitflags::bitflags;
// use untyped_arena::Arena as UntypedArena;

use crate::{
    blk_arena::BlkArena,
    block::Block,
    ty::{Type, TypeS},
    val::Val,
};

pub struct Func<'arena> {
    name: String,
    mangled: Option<String>,

    ty: Type<'arena>,
    arg_names: Vec<String>,
    attr: FuncAttr,

    /// Contains an arena for both Blocks and Isns,
    /// which form a graph between each other
    arena: &'arena BlkArena<'arena>,
    blocks: HashMap<String, &'arena Block<'arena>>,
    entry: Option<&'arena Block<'arena>>,
    arg_vals: HashMap<usize, Rc<Val<'arena>>>,
}

bitflags! {
    #[derive(Default)]
    pub struct FuncAttr: u8 {
        const INTERNAL = 1 << 0;
        const WEAK = 1 << 1;
    }
}

#[allow(dead_code)]
impl<'arena> Func<'arena> {
    pub fn name(&self) -> &str {
        &self.name
    }
    pub fn mangled_name(&self) -> Option<&str> {
        self.mangled.as_deref()
    }
    pub fn ty(&self) -> Type<'arena> {
        self.ty
    }
    pub fn arg_names(&self) -> &[String] {
        &self.arg_names
    }
    pub fn attr(&self) -> FuncAttr {
        self.attr
    }
    pub fn entry(&self) -> Option<&'arena Block<'arena>> {
        self.entry
    }
}

impl<'arena> Func<'arena> {
    pub fn arg_by_name(&self, name: &str) -> Option<(usize, Type<'arena>)> {
        self.arg_names
            .iter()
            .zip(self.arg_types())
            .enumerate()
            .find(|(_, (arg_name, _))| arg_name.as_str() == name)
            .map(|(i, (_, ty))| (i, ty))
    }

    fn arg_types(&self) -> impl Iterator<Item = Type<'arena>> {
        if let TypeS::Func { args, .. } = self.ty {
            args.iter().copied()
        } else {
            unreachable!()
        }
    }

    pub fn register_arg_val(&mut self, idx: usize, v: Rc<Val<'arena>>) {
        let old = self.arg_vals.insert(idx, v);
        assert!(old.is_none());
    }
}

impl<'arena> Func<'arena> {
    pub fn new(
        name: String,
        ty: Type<'arena>,
        arg_names: Vec<String>,
        arena: &'arena BlkArena<'arena>,
    ) -> Self {
        match ty {
            TypeS::Func {
                ret: _,
                args,
                variadic: _,
            } => {
                if !arg_names.is_empty() {
                    assert_eq!(args.len(), arg_names.len());
                }
            }
            _ => unreachable!(),
        }

        Self {
            name,
            mangled: None,
            ty,
            arg_names,
            attr: Default::default(),
            arena,
            blocks: HashMap::new(),
            entry: None,
            arg_vals: Default::default(),
        }
    }

    pub fn add_attr(&mut self, attr: FuncAttr) {
        self.attr |= attr;
    }

    pub fn get_block<'s>(&'s mut self, ident: String) -> (&'arena Block<'arena>, bool)
    where
        'arena: 's,
    {
        let mut inserted = false;

        let b = self.blocks.entry(ident).or_insert_with(|| {
            inserted = true;
            self.arena.blks.alloc(Block::new())
        });

        (b, inserted)
    }

    pub fn set_entry(&mut self, entry: &'arena Block<'arena>) {
        let old = self.entry.replace(entry);
        assert!(old.is_none());
    }
}

/*
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "mem.h"
#include "dynmap.h"

#include "function.h"
#include "function_internal.h"
#include "lbl.h"
#include "block_internal.h"
#include "block_struct.h"
#include "variable.h"
#include "variable_struct.h"
#include "function_struct.h"
#include "isn_struct.h"
#include "isn.h"
#include "mangle.h"
#if 0
#include "regalloc.h"
#endif
#include "unit_internal.h"
#include "imath.h"

struct traverse_jmpcomp
{
    dynmap *markers;
    void (*fn)(block *, void *);
    void *ctx;
};

static void function_add_block(function *, block *);

function *function_new(
        char *lbl, struct type *fnty,
        dynarray *toplvl_args,
        unsigned *uniq_counter)
{
    function *fn = xcalloc(1, sizeof *fn);

    fn->name = lbl;
    fn->fnty = fnty;
    fn->uniq_counter = uniq_counter;

    dynarray_init(&fn->arg_names);
    dynarray_move(&fn->arg_names, toplvl_args);

    fn->arg_vals = xcalloc(
            dynarray_count(&fn->arg_names), sizeof(*fn->arg_vals));

    return fn;
}

static void block_free_abi(block *b, void *ctx)
{
    (void)ctx;
    block_free(b);
}

void function_free(function *f)
{
    function_onblocks(f, block_free_abi, NULL);
    if(f->exit)
        block_free(f->exit);

    dynarray_foreach(&f->arg_names, free);
    dynarray_reset(&f->arg_names);

    mangle_free(f->name, &f->name_mangled);
    free(f->arg_vals);

    free(f->blocks);
    free(f->name);
    free(f);
}

void function_add_block(function *f, block *b)
{
    f->nblocks++;
    f->blocks = xrealloc(f->blocks, f->nblocks * sizeof *f->blocks);
    f->blocks[f->nblocks - 1] = b;
}

void function_onblocks(function *f, void cb(block *, void *), void *ctx)
{
    size_t i;
    for(i = 0; i < f->nblocks; i++){
        if(f->blocks[i] == f->exit)
            continue;
        cb(f->blocks[i], ctx);
    }
}

static bool traverse_block_marked(block *blk, dynmap *markers)
{
    if(dynmap_get(block *, int, markers, blk))
        return true;
    (void)dynmap_set(block *, int, markers, blk, 1);
    return false;
}

static void function_blocks_traverse_r(
        block *blk,
        void fn(block *, void *),
        void *ctx,
        dynmap *markers)
{
    if(traverse_block_marked(blk, markers))
        return;

    fn(blk, ctx);

    switch(blk->type){
        case BLK_UNKNOWN:
            assert(0 && "unknown block");
            break;
        case BLK_ENTRY:
        case BLK_EXIT:
            break;
        case BLK_JMP_COMP:
            break;
        case BLK_BRANCH:
            function_blocks_traverse_r(blk->u.branch.t, fn, ctx, markers);
            function_blocks_traverse_r(blk->u.branch.f, fn, ctx, markers);
            break;
        case BLK_JMP:
            function_blocks_traverse_r(blk->u.jmp.target, fn, ctx, markers);
            break;
    }
}

static void traverse_computed_goto(block *blk, void *vctx)
{
    struct traverse_jmpcomp *ctx = vctx;

    if(traverse_block_marked(blk, ctx->markers))
        return;

    ctx->fn(blk, ctx->ctx);
}

void function_blocks_traverse(
        function *func,
        void fn(block *, void *),
        void *ctx)
{
    struct traverse_jmpcomp traverse_ctx;
    dynmap *markers = BLOCK_DYNMAP_NEW();

    function_blocks_traverse_r(function_entry_block(func, false), fn, ctx, markers);

    /* cover all blocks, in case of computed-goto-otherwise-unreachable ones */
    traverse_ctx.markers = markers;
    traverse_ctx.fn = fn;
    traverse_ctx.ctx = ctx;
    function_onblocks(func, traverse_computed_goto, &traverse_ctx);

    dynmap_free(markers);
}

dynarray *function_arg_names(function *f)
{
    return &f->arg_names;
}

block *function_entry_block(function *f, bool create)
{
    if(!f->entry && create){
        f->entry = block_new_entry();
        function_add_block(f, f->entry);
    }

    return f->entry;
}

block *function_exit_block(function *f, unit *unit)
{
    if(!f->exit)
        f->exit = function_block_new(f, unit);

    return f->exit;
}

block *function_block_new(function *f, unit *unit)
{
    block *b = block_new(lbl_new_private(f->uniq_counter, unit_lbl_private_prefix(unit)));
    function_add_block(f, b);
    return b;
}

void function_block_split(function *f, unit *unit, block *blk, struct isn *first_new, block **const newblk)
{
    block *new = function_block_new(f, unit);

    /* Housekeeping 1: block lifetimes
     *
     * `val_lifetimes` is empty in `newblk`.
     * We assume function_block_split() is called before any lifetime logic (regalloc and spill)
     */
    assert(!f->lifetime_filled);

    /* Housekeeping 2: type and associated bits */
    new->type = blk->type;
    blk->type = BLK_UNKNOWN;
    memcpy(&new->u, &blk->u, sizeof(new->u));
    memset(&blk->u, 0, sizeof(blk->u));

    /* Housekeeping 3: isns */
    isns_detach(first_new);
    block_set_isns(new, first_new);

    /* Housekeeping 4: preds */
    /* not updated */

    *newblk = new;
}

block *function_block_find(
        function *f,
        unit *unit,
        char *ident /*takes ownership*/,
        int *const created)
{
    const char *prefix = unit_lbl_private_prefix(unit);
    size_t i;
    block *b;

    for(i = 0; i < f->nblocks; i++){
        const char *lbl;

        b = f->blocks[i];
        lbl = block_label(b);

        if(lbl && lbl_equal_to_ident(lbl, ident, prefix)){
            if(created)
                *created = 0;

            free(ident);
            return b;
        }
    }

    if(created)
        *created = 1;

    b = block_new(lbl_new_ident(ident, prefix));
    free(ident);
    function_add_block(f, b);
    return b;
}

static void add_arguments_to_map(function *f, dynmap *values_to_block)
{
    size_t i;
    block *entry = function_entry_block(f, false);
    assert(entry);

    dynarray_iter(&f->arg_names, i){
        val *v = f->arg_vals[i];
        if(v)
            dynmap_set(val *, block *, values_to_block, v, entry);
    }
}

void function_finalize(function *f)
{
    dynmap *values_to_block;

    if(!f->entry)
        return;

    /* find out which values live outside their block */
    values_to_block = dynmap_new(val *, NULL, val_hash);

    /* all arguments are implicitly live in the first block */
    add_arguments_to_map(f, values_to_block);

    function_onblocks(f, block_check_val_life, values_to_block);

    dynmap_free(values_to_block);
}

void function_add_attributes(function *f, enum function_attributes attr)
{
    f->attr |= attr;
}

enum function_attributes function_attributes(function *f)
{
    return f->attr;
}

static void print_func_and_args(dynarray *arg_tys, dynarray *arg_names, bool variadic, FILE *fout)
{
    const size_t nargs = dynarray_count(arg_tys);
    size_t i;

    fprintf(fout, "(");

    dynarray_iter(arg_tys, i){
        variable tmpvar;

        tmpvar.ty = dynarray_ent(arg_tys, i);

        if(dynarray_is_empty(arg_names))
            tmpvar.name = "";
        else
            tmpvar.name = dynarray_ent(arg_names, i);

        fprintf(fout, "%s%s%s%s",
                type_to_str(tmpvar.ty),
                *tmpvar.name ? " $" : "",
                tmpvar.name,
                i == nargs - 1 ? "" : ", ");
    }

    if(variadic){
        fprintf(fout, "%s...", i > 0 ? ", " : "");
    }

    fprintf(fout, ")");
}

static void block_unmark_emitted(block *blk, void *vctx)
{
    blk->emitted = 0;
}

static void block_dump_abi(block *b, void *ctx)
{
    FILE *f = ctx;
    block_dump1(b, f);
}

void function_dump(function *f, FILE *fout)
{
    fprintf(fout, "$%s = ", function_name(f));

    if(dynarray_is_empty(&f->arg_names)){
        fprintf(fout, "%s", type_to_str(function_type(f)));
    }else{
        bool variadic;
        dynarray *args;
        type *retty = type_func_call(function_type(f), &args, &variadic);

        assert(retty);
        fprintf(fout, "%s", type_to_str(retty));

        print_func_and_args(args, &f->arg_names, variadic, fout);
    }

    if(f->entry){
        fprintf(fout, "\n{\n");

        function_blocks_traverse(f, block_dump_abi, fout);
        function_blocks_traverse(f, block_unmark_emitted, NULL);

        fprintf(fout, "}");
    }

    fprintf(fout, "\n");
}

const char *function_name(function *f)
{
    return f->name;
}

const char *function_name_mangled(function *f, const struct target *target)
{
    return mangle(f->name, &f->name_mangled, target);
}

struct type *function_type(function *f)
{
    return f->fnty;
}

size_t function_arg_count(function *f)
{
    return dynarray_count(&f->arg_names);
}

bool function_arg_find(
        function *f, const char *name,
        size_t *const idx, type **const ty)
{
    size_t i;
    dynarray_iter(&f->arg_names, i){
        if(!strcmp(name, dynarray_ent(&f->arg_names, i))){
            dynarray *const arg_tys = type_func_args(f->fnty);
            *idx = i;
            *ty = dynarray_ent(arg_tys, i);
            return true;
        }
    }

    return false;
}

void function_register_arg_val(function *f, unsigned arg_idx, val *v)
{
    f->arg_vals[arg_idx] = v;
}

val *function_arg_val(function *f, unsigned arg_idx)
{
    return f->arg_vals[arg_idx];
}

#if 0
static struct location *locate_arg_reg(
        size_t idx,
        const struct backend_traits *backend)
{
    struct location *loc = xmalloc(sizeof *loc);

    if(idx >= backend->arg_regs_cnt){
        assert(0 && "TODO: arg on stack");
    }else{
        loc->where = NAME_IN_REG;
        loc->u.reg = backend->arg_regs[idx];
    }

    return loc;
}

static void assign_argument_registers(
        function *f,
        const struct backend_traits *backend)
{
    size_t i;

    dynarray_iter(&f->arg_names, i){
        struct location *arg_loc = locate_arg_reg(i, backend);

        dynarray_add(&f->arg_locns, arg_loc);
    }
}

void func_regalloc(
        function *f,
        struct regalloc_info *info,
        unsigned *const alloca)
{
    block *const entry = function_entry_block(f, false);

    assign_argument_registers(f, &info->backend);

    regalloc(entry, info, alloca);
}
#endif

unsigned function_alloc_stack_space(function *f, type *for_ty)
{
    unsigned sz, align;
    type_size_align(for_ty, &sz, &align);

    f->stackspace += sz;
    f->stackspace += gap_for_alignment(f->stackspace, align);

    return f->stackspace;
}

unsigned function_get_stack_use(function *f)
{
    return f->stackspace;
}

bool function_is_forward_decl(function *f)
{
    return !function_entry_block(f, false);
}
*/
