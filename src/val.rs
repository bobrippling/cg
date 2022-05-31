#![allow(dead_code)]

use crate::{global::Global, reg::Reg, ty::Type, ty_uniq::TyUniq};

#[derive(Debug)]
pub struct Val<'arena> {
    ty: Type<'arena>,

    /*
    enum val_flags
    {
        ABI = 1 << 0,
        ARG = 1 << 1,
        SPILL = 1 << 2
    } flags;
    */
    kind: ValKind, /*<'arena>*/
}

#[derive(Debug)]
pub enum ValKind /*<'arena>*/ {
    Literal(i32),
    // Global(&'arena Global<'arena>),
    Label(String),
    Undef,
    Local { /*loc: Option<Location>,*/ name: String },
    Alloca { loc: u32, name: String },
}

impl<'a> Val<'a> {
    pub fn new_void(_tvoid: Type<'a>) -> Val {
        todo!()
    }

    pub fn new_i(_n: i32, _ty: Type<'a>) -> Self {
        todo!()
    }

    pub fn new_undef(_ty: Type<'a>) -> Self {
        todo!()
    }

    pub fn new_argument(name: String, ty: Type<'a>) -> Self {
        Self {
            ty,
            kind: ValKind::Local { /*loc: None,*/ name },
        }
    }

    pub fn new_global(_ut: &mut TyUniq, _glob: &Global) -> Self {
        todo!()
    }

    pub fn new_label(_name: String, _ty: Type<'a>) -> Self {
        todo!()
    }

    pub fn new_local(_name: String, _ty: Type<'a>, _alloca: bool) -> Self {
        todo!()
    }

    pub fn new_reg(reg: Reg, ty: Type<'a>) -> Self {
        Self {
            ty,
            kind: ValKind::Local {
                /*loc: todo!() Some(Location {
                    constraint: Constraint::Reg,
                    loc: Loc::Reg(reg),
                }),*/
                name: "".into(),
            },
        }
    }
}

impl<'a> Val<'a> {
    pub fn ty(&self) -> Type<'a> {
        self.ty
    }

    /*
    pub fn location(&self) -> Option<Location> {
        match &self.kind {
            ValKind::Local { loc: Some(loc), .. } => {
                Some(loc.loc.clone())
            },
            ValKind::Alloca { loc, .. } => {
                Some(Loc::Spilt { offset: *loc })
            },
            _ => None,
        }
    }
    */
}

/*
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "mem.h"
#include "dynmap.h"

#include "val.h"
#include "val_internal.h"
#include "val_struct.h"

#include "variable.h"
#include "isn.h"
#include "op.h"
#include "global.h"
#include "type.h"
#include "backend_isn.h"

#if 0
static val *val_new(enum val_kind k);

static bool val_in(val *v, enum val_to to)
{
    switch(v->type){
        case INT:
            return to & LITERAL;
        case INT_PTR:
            return to & (LITERAL | ADDRESSABLE);
        case ARG:
        case NAME:
            return to & NAMED;
        case ALLOCA:
            return to & ADDRESSABLE;
        case LBL:
            return to & ADDRESSABLE;
    }
    assert(0);
}

val *val_need(val *v, enum val_to to, const char *from)
{
    if(val_in(v, to))
        return v;

    fprintf(stderr, "%s: val_need(): don't have 0x%x\n", from, to);
    assert(0);
}

int val_size(val *v, unsigned ptrsz)
{
    switch(v->type){
        case INT_PTR:
        case ALLOCA:
        case LBL:
            return ptrsz;
        case INT:
            return v->u.i.val_size;
        case ARG:
            return v->u.arg.val_size;
        case NAME:
            return v->u.addr.u.name.val_size;
    }
    assert(0);
}
#endif

bool val_is_mem(val *v)
{
    struct location *loc = val_location(v);

    switch(v->kind){
        case GLOBAL:
            assert(!loc);
            return true;
        case ALLOCA:
            return true;

        case UNDEF:
            return false;

        case LITERAL:
            /* could be mem, e.g. i4* */
            return !!type_deref(val_type(v));

        case LABEL:
        case LOCAL:
            break;
    }

    if(loc)
        return loc->where == NAME_SPILT;

    return false;
}

bool val_is_int(val *v, size_t *const out)
{
    if(v->kind != LITERAL)
        return false;

    *out = v->u.i;
    return true;
}

bool val_is_volatile(val *v)
{
    struct location *loc = val_location(v);

    return loc && location_is_reg(loc->where);
}

bool val_is_undef(val *v)
{
    return v->kind == UNDEF;
}

bool val_is_reg(val *v)
{
    struct location *loc = val_location(v);
    return loc && location_is_reg(loc->where);
}

bool val_is_reg_specific(val *v, regt reg)
{
    return val_is_reg(v) && val_location(v)->u.reg == reg;
}

bool val_is_abi(val *v)
{
    return !!(v->flags & ABI);
}

bool val_on_stack(val *v)
{
    struct location *loc = val_location(v);
    return loc && loc->where == NAME_SPILT;
}

bool val_can_be_assigned_reg(val *v)
{
    switch(v->kind){
        case LITERAL:
        case GLOBAL:
        case LABEL:
        case ALLOCA:
            return false;

        case UNDEF:
        case LOCAL:
            return true;
    }
}

bool val_can_be_assigned_mem(val *v)
{
    switch(v->kind){
        case LITERAL:
        case GLOBAL:
        case LABEL:
        case ALLOCA:
            return false;

        case UNDEF:
        case LOCAL:
            return true;
    }
}

unsigned val_hash(val *v)
{
    unsigned h = v->kind;
    const char *spel = NULL;

    switch(v->kind){
        case LITERAL:
            h ^= v->u.i;
            break;

        case UNDEF:
            h ^= 0xdead;
            break;

        case GLOBAL:
        {
            spel = global_name(v->u.global);
            break;
        }

        case LABEL:
        {
            spel = v->u.label.name;
            break;
        }

        case LOCAL:
            spel = v->u.local.name;

            if(v->flags & ABI){
                /* safe to use here - set on init */
                h ^= location_hash(&v->u.local.loc);
            }
            break;

        case ALLOCA:
            spel = v->u.alloca.name;
            break;
    }

    if(spel)
        h ^= dynmap_strhash(spel);

    return h;
}

const char *val_kind_to_str(enum val_kind k)
{
    switch(k){
        case LITERAL: return "LITERAL";
        case GLOBAL: return "GLOBAL";
        case LABEL: return "LABEL";
        case ALLOCA: return "ALLOCA";
        case LOCAL: return "LOCAL";
        case UNDEF: return "UNDEF";
    }

    return NULL;
}

struct location *val_location(val *v)
{
    switch(v->kind){
        case ALLOCA:
            return &v->u.alloca.loc;
        case LOCAL:
            return &v->u.local.loc;
        case GLOBAL:
        case LITERAL:
        case LABEL:
        case UNDEF:
            break;
    }
    return NULL;
}

enum operand_category val_operand_category(val *v, bool dereference)
{
    switch(v->kind){
        case LITERAL:
            return dereference ? OPERAND_MEM_CONTENTS : OPERAND_INT;

        case LABEL:
            assert(!dereference);
        case GLOBAL:
        case ALLOCA:
            return dereference ? OPERAND_MEM_CONTENTS : OPERAND_MEM_PTR;

        case UNDEF:
            return OPERAND_REG;

        case LOCAL: {
            struct location *loc = val_location(v);
            switch(loc->where){
                case NAME_NOWHERE:
                case NAME_IN_REG_ANY:
                case NAME_IN_REG:
                    return dereference ? OPERAND_MEM_CONTENTS : OPERAND_REG;

                case NAME_SPILT:
                    return OPERAND_MEM_CONTENTS;
            }
            break;
        }
    }
    assert(0);
}

bool val_operand_category_matches(enum operand_category a, enum operand_category b)
{
    a &= OPERAND_MASK_PLAIN;
    b &= OPERAND_MASK_PLAIN;

    return a == b;
}

#if 0
static bool val_both_ints(val *l, val *r)
{
    if(l->kind != INT || r->kind != INT)
        return false;

    assert(l->u.i.val_size == r->u.i.val_size);
    return true;
}

bool val_op_maybe(enum op op, val *l, val *r, int *res)
{
    int err;

    if(!val_both_ints(l, r))
        return false;

    *res = op_exe(op, l->u.i.i, r->u.i.i, &err);

    return !err;
}

bool val_cmp_maybe(enum op_cmp cmp, val *l, val *r, int *res)
{
    if(!val_both_ints(l, r))
        return false;

    *res = op_cmp_exe(cmp, l->u.i.i, r->u.i.i);
    return true;
}

val *val_op_symbolic(enum op op, val *l, val *r)
{
    val *res;

    if(!val_op_maybe_val(op, l, r, &res))
        return res; /* fully integral */

    /* one side must be numeric */
    val *num = NULL;
    if(l->kind == INT)
        num = l;
    if(r->kind == INT)
        num = r;
    if(!num)
        assert(0 && "symbolic op needs an int");

    val *sym = (num == l ? r : l);
    switch(sym->kind){
        case INT:
            assert(0 && "unreachable");

        case INT_PTR:
            return val_new_ptr_from_int(sym->u.i.i + num->u.i.i);

        case ALLOCA:
        {
            val *alloca = val_new(ALLOCA);
            alloca->u.addr.u.alloca.idx = sym->u.addr.u.alloca.idx + num->u.i.i;
            return alloca;
        }

        case LBL:
        case NAME:
        case ARG:
            assert(0 && "can't add to name vals");
    }
    assert(0);
}
#endif

static char *val_abi_str_r(char buf[VAL_STR_SZ], size_t n, struct location *loc)
{
    switch(loc->where){
        case NAME_NOWHERE:
            /*xsnprintf(buf, n, "<unlocated>");*/
            break;
        case NAME_IN_REG_ANY:
            xsnprintf(buf, n, "<reg ?>");
            break;
        case NAME_IN_REG:
            xsnprintf(buf, n, "<reg %d>", loc->u.reg);
            break;
        case NAME_SPILT:
            xsnprintf(buf, n, "<stack %d>", loc->u.off);
            break;
    }
    return buf;
}

char *val_str_r(char buf[VAL_STR_SZ], val *v)
{
    struct location *loc = val_location(v);

    switch(v->kind){
        case LITERAL:
            if(type_is_void(v->ty))
                xsnprintf(buf, VAL_STR_SZ, "void");
            else
                xsnprintf(buf, VAL_STR_SZ, "%s %d", type_to_str(v->ty), v->u.i);
            break;
        case GLOBAL:
            xsnprintf(buf, VAL_STR_SZ, "$%s", global_name(v->u.global));
            break;
        case LABEL:
            xsnprintf(buf, VAL_STR_SZ, "$%s", v->u.label.name);
            break;
        case LOCAL:
            if(v->u.local.name)
                xsnprintf(buf, VAL_STR_SZ, "$%s%s", v->u.local.name, v->flags & ABI ? "<abi>" : "");
            else
                xsnprintf(buf, VAL_STR_SZ, "$anon_%p", v);
            break;
        case ALLOCA:
            xsnprintf(buf, VAL_STR_SZ, "$%s", v->u.alloca.name);
            break;
        case UNDEF:
            xsnprintf(buf, VAL_STR_SZ, "undef");
            break;
    }

    if(loc){
        const size_t len = strlen(buf);
        val_abi_str_r(buf + len, VAL_STR_SZ - len, loc);
    }

    return buf;
}

char *val_str(val *v)
{
    static char buf[VAL_STR_SZ];
    return val_str_r(buf, v);
}

char *val_str_rn(unsigned buf_n, val *v)
{
    static char buf[3][VAL_STR_SZ];
    assert(buf_n < 3);
    return val_str_r(buf[buf_n], v);
}

static val *val_new(enum val_kind k, struct type *ty)
{
    val *v = xcalloc(1, sizeof *v);
    /* v->retains begins at zero - isns initially retain them */
    v->kind = k;
    v->ty = ty;
    return v;
}

val *val_retain(val *v)
{
    /* valid for v->retains to be 0 */
    v->retains++;
    return v;
}

static void val_free(val *v)
{
    switch(v->kind){
        case LITERAL:
            break;
        case GLOBAL:
            break;
        case LABEL:
            free(v->u.label.name);
            break;
        case LOCAL:
            free(v->u.local.name);
            break;
        case ALLOCA:
            free(v->u.alloca.name);
            break;
        case UNDEF:
            break;
    }
    free(v);
}

void val_mirror(val *dest, val *src)
{
    /* don't change retains, type, or pass_data */
    dest->u = src->u;
    dest->live_across_blocks = src->live_across_blocks;
    dest->kind = src->kind;
}

void val_release(val *v)
{
    assert(v->retains > 0 && "unretained val");

    v->retains--;

    if(v->retains == 0)
        val_free(v);
}

#if 0
val *val_name_new(unsigned sz, char *ident)
{
    val *v = val_new(NAME);

    if(!ident){
        /* XXX: static */
        static int n;
        char buf[32];
        xsnprintf(buf, sizeof buf, "tmp_%d", n++);
        ident = xstrdup(buf);
    }

    v->u.addr.u.name.spel = ident;
    v->u.addr.u.name.loc.where = NAME_IN_REG;
    v->u.addr.u.name.loc.u.reg = -1;
    v->u.addr.u.name.val_size = sz;

    return v;
}
#endif

val *val_new_i(int i, struct type *ty)
{
    val *p = val_new(LITERAL, ty);
    p->u.i = i;
    return p;
}

val *val_new_void(struct uniq_type_list *us)
{
    val *v = val_new(LITERAL, type_get_void(us));
    v->u.i = 0;
    return v;
}

val *val_new_undef(struct type *ty)
{
    val *v = val_new(UNDEF, ty);
    return v;
}

val *val_new_argument(char *name, struct type *ty)
{
    val *v = val_new_local(name, ty, false);
    v->flags |= ARG;
    return v;
}

val *val_new_global(struct uniq_type_list *us, struct global *glob)
{
    val *p = val_new(GLOBAL, global_type_as_ptr(us, glob));
    p->u.global = glob;
    return p;
}

val *val_new_local(char *name, struct type *ty, bool alloca)
{
    val *p = val_new(alloca ? ALLOCA : LOCAL, ty);
    p->u.local.name = name;
    location_init_reg(&p->u.local.loc);

    if(alloca)
        assert(type_deref(ty));

    return p;
}

val *val_new_label(char *name, struct type *ty)
{
    val *p = val_new(LABEL, ty);
    p->u.label.name = name;
    return p;
}

val *val_new_localf(struct type *ty, bool alloca, const char *fmt, ...)
{
    va_list l;
    char *buf;

    va_start(l, fmt);
    buf = xvsprintf(fmt, l);
    va_end(l);

    return val_new_local(buf, ty, alloca);
}

const char *val_frontend_name(val *v)
{
    switch(v->kind){
        case LOCAL: return v->u.local.name;
        case ALLOCA: return v->u.alloca.name;
        case UNDEF: return "undef";

        case LITERAL:
        case GLOBAL:
        case LABEL:
            break;
    }
    return NULL;
}

void val_temporary_init(val *vtmp, type *ty)
{
    assert(ty);

    memset(vtmp, 0, sizeof *vtmp);

    vtmp->kind = LOCAL;
    vtmp->ty = ty;
    vtmp->retains = 1;
    location_init_reg(&vtmp->u.local.loc);
}

val *val_new_reg(regt reg, type *ty)
{
    val *p = val_new(LOCAL, ty);
    p->u.local.loc.where = NAME_IN_REG;
    p->u.local.loc.u.reg = reg;
    p->u.local.loc.constraint = CONSTRAINT_REG;
    p->flags |= ABI;
    return p;
}

val *val_new_stack(int stack_off, type *ty)
{
    val *p = val_new(LOCAL, ty);
    p->u.local.loc.where = NAME_SPILT;
    p->u.local.loc.u.off = stack_off;
    p->u.local.loc.constraint = CONSTRAINT_MEM;
    p->flags |= ABI;
    return p;
}

struct type *val_type(val *v)
{
    return v->ty;
}

void val_size_align(val *v, unsigned *const s, unsigned *const a)
{
    type_size_align(val_type(v), s, a);
}

unsigned val_size(val *v)
{
    return type_size(val_type(v));
}
*/
