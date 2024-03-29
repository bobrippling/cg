use std::{
    cell::{Ref, RefCell, RefMut},
    rc::Weak,
    hash::Hash,
    ptr,
};

use crate::{isn::Isn, dag::Dag};

#[derive(Debug)]
pub struct Block<'arena> {
    inner: RefCell<BlockInner<'arena>>,
}

pub type PBlock<'arena> = &'arena Block<'arena>;

#[derive(Debug)]
struct BlockInner<'arena> {
    isns: Vec<Isn<'arena>>,
    label: Option<String>, /* None if entry block */

    dag: Option<Dag>,

    // val_lifetimes: HashMap<Val, Lifetime>,
    // predecessors: Vec<Weak<Block>>,
    kind: BlockKind<'arena>,
}

#[allow(dead_code)]
#[derive(Debug)]
pub enum BlockKind<'arena> {
    Unknown,
    Entry,
    Exit,
    EntryExit,
    Branch {
        // cond: Val,
        t: Weak<Block<'arena>>,
        f: Weak<Block<'arena>>,
    },
    Jmp {
        target: Weak<Block<'arena>>,
    },
    JmpComp {
        target: Weak<Block<'arena>>,
    },
}

impl<'arena> Block<'arena> {
    pub fn new_labelled(s: String) -> Self {
        Self::new(Some(s), BlockKind::Unknown)
    }

    pub fn new_entry() -> Self {
        Self::new(None, BlockKind::Entry)
    }

    fn new(label: Option<String>, kind: BlockKind<'arena>) -> Self {
        Block {
            inner: RefCell::new(BlockInner {
                isns: vec![],
                dag: None,
                label,
                kind,
            }),
        }
    }

    fn from_inner<F, R>(&self, f: F) -> Ref<R>
    where
        F: for<'a> FnOnce(&'a BlockInner<'arena>) -> &'a R,
    {
        let r = self.inner.borrow();
        Ref::map(r, f)
    }

    // pub fn set_label(&mut self, label: String) {
    //     self.inner.borrow_mut().label = Some(label);
    // }

    pub fn label(&self) -> Ref<Option<String>> {
        self.from_inner(|inner| &inner.label)
    }

    #[cfg(test)]
    pub fn kind(&self) -> Ref<BlockKind<'arena>> {
        self.from_inner(|inner| &inner.kind)
    }

    pub fn is_tenative(&self) -> bool {
        todo!()
    }

    pub fn is_unknown_ending(&self) -> bool {
        matches!(self.inner.borrow().kind, BlockKind::Unknown)
    }

    pub fn add_isn(&self, isn: Isn<'arena>) {
        self.inner.borrow_mut().isns.push(isn);
    }

    pub fn isns(&self) -> Ref<Vec<Isn<'arena>>> {
        self.from_inner(|inner| &inner.isns)
    }

    pub fn isns_mut(&self) -> RefMut<Vec<Isn<'arena>>> {
        let r = self.inner.borrow_mut();
        RefMut::map(r, |inner| &mut inner.isns)
    }

    pub fn set_jmp(&self, _block: &Block) {
        todo!()
    }

    pub fn set_exit(&self) {
        let mut inner = self.inner.borrow_mut();

        inner.kind = if matches!(inner.kind, BlockKind::Entry) {
            BlockKind::EntryExit
        } else {
            BlockKind::Exit
        };
    }

    pub fn set_dag(&self, dag: Dag) {
        let old = self.inner.borrow_mut().dag.replace(dag);
        assert!(old.is_none());
    }
}

impl Hash for Block<'_> {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        ptr::hash(self, state);
    }
}

impl PartialEq for Block<'_> {
    fn eq(&self, other: &Self) -> bool {
        ptr::eq(self, other)
    }
}
impl Eq for Block<'_> {}

/*
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "mem.h"
#include "dynmap.h"

#include "block.h"
#include "block_struct.h"
#include "block_internal.h"
#include "function.h"

#include "isn_struct.h"
#include "isn.h"

#include "val.h"
#include "val_internal.h"
#include "val_struct.h"

struct life_check_ctx
{
    block *blk;
    dynmap *values_to_block;
};

block *block_new(char *lbl)
{
    block *b = xcalloc(1, sizeof *b);
    b->lbl = lbl;

    b->val_lifetimes = dynmap_new(val *, NULL, val_hash);

    return b;
}

static void branch_free(block *b)
{
    val_release(b->u.branch.cond);
}

static void block_rewind_isnhead(block *b)
{
    if(b->isnhead)
        b->isnhead = isn_first(b->isnhead);
}

void block_free(block *b)
{
    size_t i;
    struct lifetime *lt;

    if(b->type == BLK_BRANCH)
        branch_free(b);

    dynarray_reset(&b->preds);

    for(i = 0; (lt = dynmap_value(struct lifetime *, b->val_lifetimes, i)); i++){
        val *v = dynmap_key(val *, b->val_lifetimes, i);
        val_release(v);
        free(lt);
    }

    dynmap_free(b->val_lifetimes);

    block_rewind_isnhead(b);
    isn_free_r(b->isnhead);
    free(b->lbl);
    free(b);
}

block *block_new_entry(void)
{
    block *b = block_new(NULL);
    return b;
}

dynmap *block_lifetime_map(block *b)
{
    return b->val_lifetimes;
}

const char *block_label(block *b)
{
    return b->lbl;
}

isn *block_first_isn(block *b)
{
    block_rewind_isnhead(b);
    return b->isnhead;
}

void block_add_isn(block *b, struct isn *i)
{
    if(!b){
        /* unreachable code */
        isn_free_1(i);
        return;
    }

    if(!b->isnhead){
        assert(!b->isntail);
        b->isnhead = i;
        b->isntail = i;
        return;
    }

    assert(b->isntail);

    isn_insert_after(b->isntail, i);
    b->isntail = i;
}

void block_add_isns(block *b, struct isn *i)
{
    if(!b){
        /* unreachable code */
        isn_free_r(i);
        return;
    }

    if(!b->isnhead){
        assert(!b->isntail);
        b->isnhead = i;
        b->isntail = isn_last(i);
        return;
    }

    isns_insert_after(b->isntail, i);
    b->isntail = isn_last(i);
}

void block_set_isns(block *b, struct isn *i)
{
    if(!b){
        /* unreachable code */
        isn_free_1(i);
        return;
    }

    assert(!b->isnhead);
    assert(!b->isntail);

    assert(!i->prev);

    b->isnhead = i;
    b->isntail = isn_last(i);
}

int block_tenative(block *b)
{
    return block_first_isn(b) == NULL;
}

bool block_unknown_ending(block *blk)
{
    assert(blk);
    return blk->type == BLK_UNKNOWN;
}

void block_set_type(block *blk, enum block_type type)
{
    if(!blk)
        return;

    assert(block_unknown_ending(blk));
    blk->type = type;
}

void block_set_branch(block *current, val *cond, block *btrue, block *bfalse)
{
    if(!current)
        return;

    block_set_type(current, BLK_BRANCH);

    block_add_pred(btrue, current);
    block_add_pred(bfalse, current);

    current->u.branch.cond = val_retain(cond);
    current->u.branch.t = btrue;
    current->u.branch.f = bfalse;
}

void block_set_jmp(block *current, block *new)
{
    if(!current)
        return;

    block_set_type(current, BLK_JMP);
    block_add_pred(new, current);
    current->u.jmp.target = new; /* weak ref */
}

void block_add_pred(block *b, block *pred)
{
    dynarray_add(&b->preds, pred);
}

static void check_val_life_isn(val *v, isn *isn, void *vctx)
{
    struct life_check_ctx *ctx = vctx;
    block *val_block;

    (void)isn;

    if(v->live_across_blocks)
        return; /* already confirmed */

    val_block = dynmap_get(val *, block *, ctx->values_to_block, v);
    if(val_block){
        /* value is alive in 'val_block' - if it's not the same as
         * the current block then the value lives outside its block */

        if(val_block != ctx->blk){
            v->live_across_blocks = true;
        }else{
            /* encountered again in the same block. nothing to do */
        }
    }else{
        /* first encountering val. mark as alive in current block */
        dynmap_set(val *, block *, ctx->values_to_block, v, ctx->blk);
    }
}

void block_check_val_life(block *blk, void *vctx)
{
    dynmap *values_to_block = vctx;
    struct life_check_ctx ctx_lifecheck;
    isn *isn;

    ctx_lifecheck.blk = blk;
    ctx_lifecheck.values_to_block = values_to_block;

    for(isn = block_first_isn(blk); isn; isn = isn->next)
        isn_on_live_vals(isn, check_val_life_isn, &ctx_lifecheck);
}

unsigned block_hash(block *b)
{
    return (b->lbl ? dynmap_strhash(b->lbl) : 0) ^ (unsigned)b;
}

void block_dump1(block *blk, FILE *f)
{
    if(blk->emitted)
        return;
    blk->emitted = 1;

    if(blk->lbl)
        fprintf(f, "\n");

    if(!dynarray_is_empty(&blk->preds)){
        size_t i;
        const char *comma = "";

        fprintf(f, "# predecessors: ");

        dynarray_iter(&blk->preds, i){
            block *pred = dynarray_ent(&blk->preds, i);

            fprintf(f, "%s%p", comma, pred);
            comma = ", ";
        }

        fputc('\n', f);
    }

    fprintf(f, "# block: %p\n", blk);
    if(blk->lbl)
        fprintf(f, "$%s:\n", blk->lbl);
    isn_dump(block_first_isn(blk), blk, f);
}
*/
