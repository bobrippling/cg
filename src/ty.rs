use std::{ptr, num::NonZeroU32};

use crate::size_align::{self, SizeAlign, Align};

pub type Type<'t> = &'t TypeS<'t>;

#[derive(Debug, Hash)]
pub enum TypeS<'t> {
    Void,
    Primitive(Primitive),
    Ptr {
        pointee: Type<'t>,
        sz: NonZeroU32,
    },
    Array {
        elem: Type<'t>,
        n: usize,
    },
    Func {
        ret: Type<'t>,
        args: Vec<Type<'t>>,
        variadic: bool,
    },
    Struct {
        membs: Vec<Type<'t>>,
    },
    Alias {
        name: String,
        actual: Type<'t>,
    },
}

macro_rules! primitives {
    (
        pub enum Primitive {
            $($member:ident => { size: $size:expr, align: $align:expr, integral: $int:expr }),*
            $(,)*
        }
    ) => {
        #[derive(Copy, Clone, Hash, Eq, PartialEq, Debug)]
        pub enum Primitive {
            $($member),*
        }

	#[allow(dead_code)]
        impl Primitive {
            pub fn is_integral(self) -> bool {
                match self {
                    $(Primitive::$member => $int),*
                }
            }

            pub fn is_float(self) -> bool {
		!self.is_integral()
            }

            pub fn size_align(self) -> SizeAlign {
                match self {
                    $(Primitive::$member => SizeAlign {
			size: $size,
			align: Align::new($align).unwrap(),
                    }),*
                }
            }
        }
    }
}

primitives! {
    pub enum Primitive {
        I1 => { size: 1, align: 1, integral: true },
        I2 => { size: 2, align: 2, integral: true },
        I4 => { size: 4, align: 4, integral: true },
        I8 => { size: 8, align: 8, integral: true },
        F4 => { size: 4, align: 4, integral: false },
        F8 => { size: 8, align: 8, integral: false },
        FLarge => { size: 16, align: 16, integral: false },
    }
}

pub trait TypeQueries<'t>: Sized {
    fn array_elem(self) -> Option<Type<'t>>;
    fn called(self) -> Option<Type<'t>>;
    fn deref(self) -> Option<Type<'t>>;

    fn as_primitive(self) -> Option<Primitive>;

    fn size(self) -> usize {
        self.size_align().size
    }

    fn size_align(self) -> SizeAlign;

    fn is_fn(self) -> bool;
    fn is_struct(self) -> bool;
    fn is_void(self) -> bool;
    fn is_integral(self) -> bool;
    fn is_float(self) -> bool;

    fn can_return_to(self, to: Type<'t>) -> bool;
}

impl<'t> TypeS<'t> {
    pub fn resolve(mut self: &'t Self) -> &'t Self {
        while let TypeS::Alias { actual, .. } = self {
            self = actual;
        }
        self
    }
}

impl<'t> TypeQueries<'t> for Type<'t> {
    fn array_elem(self) -> Option<Self> {
        if let TypeS::Array { elem, n: _ } = self {
            Some(elem)
        } else {
            None
        }
    }

    fn called(self) -> Option<Self> {
        if let TypeS::Func {
            ret,
            args: _,
            variadic: _,
        } = self.resolve()
        {
            Some(ret)
        } else {
            None
        }
    }

    fn deref(self) -> Option<Self> {
        if let TypeS::Ptr { pointee, sz: _ } = self.resolve() {
            Some(pointee)
        } else {
            None
        }
    }

    fn as_primitive(self) -> Option<Primitive> {
        if let TypeS::Primitive(p) = self.resolve() {
            Some(*p)
        } else {
            None
        }
    }

    fn size_align(self) -> SizeAlign {
        match self {
            TypeS::Void => SizeAlign::default(),
            TypeS::Primitive(p) => p.size_align(),
            &TypeS::Ptr { sz, .. } => SizeAlign::from_size(sz),
            &TypeS::Array { elem, n } => elem.size_align() * n,
            TypeS::Func { .. } => unreachable!(),
            TypeS::Struct { membs } => {
                let mut sz_align = SizeAlign::default();

                for memb in membs {
                    let elem = memb.size_align();

                    let gap = size_align::gap_for_alignment(elem.size as _, elem.align);

                    sz_align.size += gap + elem.size;
                    sz_align.align = sz_align.align.max(elem.align);
                }

                sz_align
            }
            TypeS::Alias { actual, .. } => actual.size_align(),
        }
    }

    fn is_fn(self) -> bool {
        matches!(self, TypeS::Func { .. })
    }

    fn is_struct(self) -> bool {
        matches!(self, TypeS::Struct { .. })
    }

    fn is_void(self) -> bool {
        matches!(self, TypeS::Void { .. })
    }

    fn is_integral(self) -> bool {
        matches!(self, TypeS::Primitive(p) if p.is_integral())
    }

    fn is_float(self) -> bool {
        matches!(self, TypeS::Primitive(p) if p.is_float())
    }

    fn can_return_to(self, to: Type<'t>) -> bool {
        if to.is_struct() {
            self.deref().map(|pointee| pointee == to).unwrap_or(false)
        } else {
            self == to
        }
    }
}

macro_rules! forward {
    ($(fn $name:ident($self:ident) -> $ret:ty;)*) => {
	$(
	    fn $name($self) -> $ret {
		$self.and_then(TypeQueries::$name)
	    }
	)*
    }
}

impl<'t> TypeQueries<'t> for Option<Type<'t>> {
    forward! {
    fn array_elem(self) -> Option<Type<'t>>;
    fn called(self) -> Option<Type<'t>>;
    fn as_primitive(self) -> Option<Primitive>;
    fn deref(self) -> Option<Type<'t>>;
    }

    fn size_align(self) -> SizeAlign {
        self.map(TypeQueries::size_align).unwrap_or_default()
    }

    fn is_fn(self) -> bool {
        self.map(TypeQueries::is_fn).unwrap_or(false)
    }

    fn is_struct(self) -> bool {
        self.map(TypeQueries::is_struct).unwrap_or(false)
    }

    fn is_void(self) -> bool {
        self.map(TypeQueries::is_void).unwrap_or(false)
    }

    fn is_integral(self) -> bool {
	self.map(TypeQueries::is_integral).unwrap_or(false)
    }

    fn is_float(self) -> bool {
	self.map(TypeQueries::is_float).unwrap_or(false)
    }

    fn can_return_to(self, to: Type<'t>) -> bool {
        self.map(|t| t.can_return_to(to)).unwrap_or(false)
    }
}

impl PartialEq for TypeS<'_> {
    fn eq(&self, other: &Self) -> bool {
        let a = self.resolve();
        let b = other.resolve();
        ptr::eq(a, b)
    }
}

impl Eq for TypeS<'_> {}

/*
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include <strbuf_fixed.h>

#include "dynmap.h"

#include "mem.h"
#include "macros.h"
#include "imath.h"

#include "type.h"
#include "type_free.h"
#include "uniq_type_list_struct.h"

struct uptype
{
    type *ptrto;
    dynarray arrays;
    dynarray funcs;
};

struct type
{
    struct uptype up;

    union
    {
        enum type_primitive prim;
        struct
        {
            type *pointee;
            struct uniq_type_list *uniqs;
        } ptr;
        struct
        {
            type *elem; /* array */
            unsigned long n;
        } array;
        struct
        {
            type *ret;
            dynarray args;
            bool variadic;
        } func;
        struct
        {
            dynarray membs;
        } struct_;
        struct
        {
            char *name;
            type *actual;
        } alias;
    } u;

    enum type_kind
    {
        PRIMITIVE, PTR, ARRAY, STRUCT, FUNC, VOID, ALIAS
    } kind;
};

static type *type_resolve(type *t)
{
    while(t && t->kind == ALIAS)
        t = t->u.alias.actual;
    return t;
}

bool type_is_fn(type *t)
{
    t = type_resolve(t);
    return t && t->kind == FUNC;
}

bool type_is_fn_variadic(type *t)
{
    t = type_resolve(t);
    return type_is_fn(t) && t->u.func.variadic;
}

bool type_is_struct(type *t)
{
    t = type_resolve(t);
    return t && t->kind == STRUCT;
}

bool type_eq(type *t1, type *t2)
{
    t1 = type_resolve(t1);
    t2 = type_resolve(t2);
    return t1 == t2;
}

const enum type_primitive *type_primitive(type *t)
{
    t = type_resolve(t);
    if(!t)
        return NULL;

    if(t->kind != PRIMITIVE)
        return NULL;

    return &t->u.prim;
}

bool type_is_primitive(type *t, enum type_primitive prim)
{
    const enum type_primitive *p = type_primitive(t);
    return p && *p == prim;
}

bool type_is_int(type *t)
{
    t = type_resolve(t);
    if(!t)
        return false;

    if(t->kind != PRIMITIVE)
        return false;
    switch(t->u.prim){
#define X(name, integral, sz, align) case name: return integral;
        TYPE_PRIMITIVES
#undef X
    }
    assert(0);
}

bool type_is_float(type *t, int include_flarge)
{
    t = type_resolve(t);
    if(!t)
        return false;

    if(t->kind != PRIMITIVE)
        return false;
    switch(t->u.prim){
#define X(name, integral, sz, align) \
        case name: \
            return !integral && (include_flarge || name != flarge);

        TYPE_PRIMITIVES
#undef X
    }
    assert(0);
}

bool type_is_void(type *t)
{
    t = type_resolve(t);
    if(!t)
        return false;

    return t->kind == VOID;
}

static const char *type_primitive_to_str(enum type_primitive p)
{
    switch(p){
#define X(name, integral, s, a) case name: return #name;
        TYPE_PRIMITIVES
#undef X
    }

    return NULL;
}

static void type_primitive_size_align(
        enum type_primitive p, unsigned *sz, unsigned *align)
{
    switch(p){
#define X(name, integral, s, a) case name: *sz = s; *align = a; return;
        TYPE_PRIMITIVES
#undef X
    }
    assert(0);
}

unsigned type_primitive_size(enum type_primitive p)
{
    unsigned sz, align;
    type_primitive_size_align(p, &sz, &align);
    return sz;
}

enum type_primitive type_primitive_less_or_equal(unsigned sz, bool fp)
{
    if(fp){
        if(sz <= 4)
            return f4;
        if(sz <= 8)
            return f8;
        return flarge;
    }

    if(sz <= 1)
        return i1;
    if(sz <= 2)
        return i2;
    if(sz <= 4)
        return i4;
    return i8;
}

static bool type_to_strbuf(strbuf_fixed *const buf, type *t)
{
    switch(t->kind){
        case ALIAS:
            return strbuf_fixed_printf(buf, "$%s", t->u.alias.name);

        case VOID:
            return strbuf_fixed_printf(buf, "void");

        case PRIMITIVE:
            return strbuf_fixed_printf(buf, "%s", type_primitive_to_str(t->u.prim));

        case PTR:
            return type_to_strbuf(buf, t->u.ptr.pointee)
                && strbuf_fixed_printf(buf, "*");

        case ARRAY:
        {
            if(!strbuf_fixed_printf(buf, "[")){
                return false;
            }

            return type_to_strbuf(buf, t->u.array.elem)
                && strbuf_fixed_printf(buf, " x %lu]", t->u.array.n);
        }

        case STRUCT:
        {
            size_t i;
            const char *comma = "";

            if(!strbuf_fixed_printf(buf, "{"))
                return false;

            dynarray_iter(&t->u.struct_.membs, i){
                type *ent = dynarray_ent(&t->u.struct_.membs, i);

                if(!strbuf_fixed_printf(buf, "%s", comma))
                    return false;

                if(!type_to_strbuf(buf, ent))
                    return false;

                comma = ", ";
            }

            return strbuf_fixed_printf(buf, "}");
        }

        case FUNC:
        {
            const char *comma = "";
            size_t i;

            if(!type_to_strbuf(buf, t->u.func.ret))
                return false;

            if(!strbuf_fixed_printf(buf, "("))
                return false;

            dynarray_iter(&t->u.func.args, i){
                type *arg_ty = dynarray_ent(&t->u.func.args, i);

                if(!strbuf_fixed_printf(buf, "%s", comma))
                    return false;

                if(!type_to_strbuf(buf, arg_ty))
                    return false;

                comma = ", ";
            }

            if(t->u.func.variadic){
                if(!strbuf_fixed_printf(
                            buf,
                            "%s...",
                            dynarray_is_empty(&t->u.func.args) ? "" : ", "))
                {
                    return false;
                }
            }

            return strbuf_fixed_printf(buf, ")");
        }
    }

    assert(0);
}

const char *type_to_str_r(char *buf, size_t buflen, type *t)
{
    strbuf_fixed strbuf = STRBUF_FIXED_INIT(buf, buflen);

    if(!type_to_strbuf(&strbuf, t))
        strcpy(buf, "<type trunc>");

    return strbuf_fixed_detach(&strbuf);
}

const char *type_to_str(type *t)
{
    static char buf[256];

    return type_to_str_r(buf, sizeof buf, t);
}

static type *tnew(enum type_kind kind)
{
    type *t = xmalloc(sizeof *t);

    memset(&t->up, 0, sizeof t->up);

    t->kind = kind;

    return t;
}

type *type_get_void(uniq_type_list *us)
{
    if(us->tvoid)
        return us->tvoid;

    us->tvoid = tnew(VOID);
    return us->tvoid;
}

type *type_get_primitive(uniq_type_list *us, enum type_primitive prim)
{
    if(us->primitives[prim])
        return us->primitives[prim];

    us->primitives[prim] = tnew(PRIMITIVE);
    us->primitives[prim]->u.prim = prim;

    return us->primitives[prim];
}

type *type_get_sizet(uniq_type_list *us)
{
    enum type_primitive p;
    switch(us->ptrsz){
        case 1: p = i1; break;
        case 2: p = i2; break;
        case 4: p = i4; break;
        case 8: p = i8; break;
        default: assert(0);
    }
    return type_get_primitive(us, p);
}

type *type_get_ptr(uniq_type_list *us, type *t)
{
    if(t->up.ptrto)
        return t->up.ptrto;

    t->up.ptrto = tnew(PTR);

    t->up.ptrto->u.ptr.uniqs = us;
    t->up.ptrto->u.ptr.pointee = t;

    return t->up.ptrto;
}

type *type_get_array(uniq_type_list *us, type *t, unsigned long n)
{
    size_t i;
    type *array;

    (void)us;

    dynarray_iter(&t->up.arrays, i){
        type *ent = dynarray_ent(&t->up.arrays, i);

        assert(ent->kind == ARRAY);
        if(ent->u.array.n == n)
            return ent;
    }

    array = tnew(ARRAY);
    array->u.array.n = n;
    array->u.array.elem = t;

    dynarray_add(&t->up.arrays, array);

    return array;
}

type *type_get_func(uniq_type_list *us, type *ret, /*consumed*/dynarray *args, bool variadic)
{
    size_t i;
    type *func;

    (void)us;

    dynarray_iter(&ret->up.funcs, i){
        type *ent = dynarray_ent(&ret->up.funcs, i);

        assert(ent->kind == FUNC);
        if(ent->u.func.ret == ret
        && ent->u.func.variadic == variadic
        && dynarray_refeq(&ent->u.func.args, args))
        {
            dynarray_reset(args);
            return ent;
        }
    }

    func = tnew(FUNC);
    dynarray_init(&func->u.func.args);
    dynarray_move(&func->u.func.args, args);
    func->u.func.ret = ret;
    func->u.func.variadic = variadic;

    dynarray_add(&ret->up.funcs, func);

    return func;
}

type *type_get_struct(uniq_type_list *us, dynarray *membs)
{
    type *new;
    size_t i;

    dynarray_iter(&us->structs, i){
        type *ent = dynarray_ent(&us->structs, i);

        if(dynarray_refeq(&ent->u.struct_.membs, membs)){
            dynarray_reset(membs);
            return ent;
        }
    }

    new = tnew(STRUCT);
    dynarray_init(&new->u.struct_.membs);
    dynarray_move(&new->u.struct_.membs, membs);

    dynarray_add(&us->structs, new);

    return new;
}


type *type_deref(type *t)
{
    t = type_resolve(t);
    if(!t)
        return NULL;

    if(t->kind != PTR)
        return NULL;

    return t->u.ptr.pointee;
}

type *type_func_call(type *t, dynarray **const args, bool *const variadic)
{
    t = type_resolve(t);
    if(variadic)
        *variadic = false;

    if(!t)
        return NULL;

    if(t->kind != FUNC)
        return NULL;

    if(args){
        *args = &t->u.func.args;
    }

    if(variadic)
        *variadic = t->u.func.variadic;

    return t->u.func.ret;
}

dynarray *type_func_args(type *t)
{
    dynarray *args;
    t = type_resolve(t);
    if(!t)
        return NULL;

    type_func_call(t, &args, NULL);
    return args;
}

type *type_array_element(type *t)
{
    t = type_resolve(t);
    if(!t)
        return NULL;

    if(t->kind != ARRAY)
        return NULL;

    return t->u.array.elem;
}

type *type_struct_element(type *t, size_t i)
{
    size_t n;

    t = type_resolve(t);
    if(!t)
        return NULL;

    if(t->kind != STRUCT)
        return NULL;

    n = dynarray_count(&t->u.struct_.membs);

    if(i >= n)
        return NULL;

    return dynarray_ent(&t->u.struct_.membs, i);
}

unsigned type_struct_offset(type *sty, const size_t at)
{
    unsigned i, off, next = 0;

    assert(type_struct_element(sty, at));

    for(i = 0; i <= at; i++){
        type *t = type_struct_element(sty, i);
        unsigned sz, align;

        type_size_align(t, &sz, &align);

        off = next;
        off += gap_for_alignment(next, align);

        next += sz;
    }

    return off;
}

size_t type_array_count(type *t)
{
    t = type_resolve(t);
    assert(t && t->kind == ARRAY);

    return t->u.array.n;
}

struct typealias *type_alias_add(uniq_type_list *us, char *name /* consumed */)
{
    type *t = tnew(ALIAS);
    type *old;

    t->u.alias.name = name;
    t->u.alias.actual = NULL;

    if(!us->aliases)
        us->aliases = dynmap_new(const char *, strcmp, dynmap_strhash);

    old = dynmap_set(char *, type *, us->aliases, name, t);

    assert(!old && "already have type");

    /* struct typealias is never completed - API safety only */
    return (struct typealias *)t;
}

type *type_alias_complete(struct typealias *talias, type *actual)
{
    type *alias = (type *)talias;
    alias->u.alias.actual = actual;
    return alias;
}

type *type_alias_find(uniq_type_list *us, const char *name)
{
    return dynmap_get(const char *, type *, us->aliases, name);
}

const char *type_alias_name(type *t)
{
    assert(t->kind == ALIAS);
    return t->u.alias.name;
}

type *type_alias_resolve(type *t)
{
    assert(t->kind == ALIAS);
    return t->u.alias.actual;
}

void type_size_align(type *ty, unsigned *sz, unsigned *align)
{
    switch(ty->kind){
        case PRIMITIVE:
            type_primitive_size_align(ty->u.prim, sz, align);
            break;

        case ALIAS:
            type_size_align(ty->u.alias.actual, sz, align);
            break;

        case PTR:
            *sz = ty->u.ptr.uniqs->ptrsz;
            *align = ty->u.ptr.uniqs->ptralign;
            break;

        case ARRAY:
        {
            unsigned elemsz, elemalign;

            type_size_align(ty->u.array.elem, &elemsz, &elemalign);

            *sz = ty->u.array.n * elemsz;
            *align = elemalign;
            break;
        }

        case STRUCT:
        {
            size_t i;

            *sz = 0;
            *align = 1;

            dynarray_iter(&ty->u.struct_.membs, i){
                unsigned elemsz, elemalign;
                unsigned gap;
                type *ent = dynarray_ent(&ty->u.struct_.membs, i);

                type_size_align(ent, &elemsz, &elemalign);

                gap = gap_for_alignment(*sz, elemalign);

                *sz += gap + elemsz;

                if(elemalign > *align)
                    *align = elemalign;
            }
            break;
        }

        case FUNC:
        {
            assert(0 && "size of func");
            break;
        }

        case VOID:
        {
            *sz = *align = 0;
            break;
        }
    }
}

unsigned type_size(type *t)
{
    unsigned sz, align;
    type_size_align(t, &sz, &align);
    return sz;
}

unsigned type_align(type *t)
{
    unsigned sz, align;
    type_size_align(t, &sz, &align);
    return align;
}

static void uptype_free(struct uptype *up)
{
    if(!up)
        return;

    type_free_1(up->ptrto);
    up->ptrto = NULL;

    type_free_dynarray_1(&up->arrays);

    type_free_dynarray_1(&up->funcs);
}

void type_free_1(type *t)
{
    if(!t)
        return;

    uptype_free(&t->up);

    /* a function, array and pointer's sub-types will always be
     * below them in the tree, so we don't free them */
    switch(t->kind){
        case PRIMITIVE:
            break;
        case STRUCT:
            dynarray_reset(&t->u.struct_.membs);
            break;
        case ALIAS:
            free(t->u.alias.name);
            break;
        case PTR:
            break;
        case ARRAY:
            break;
        case FUNC:
            dynarray_reset(&t->u.func.args);
            break;
        case VOID:
            break;
    }
    free(t);
}

void type_free_dynarray_1(dynarray *da)
{
    size_t i;
    dynarray_iter(da, i){
        type *ent = dynarray_ent(da, i);
        type_free_1(ent);
    }
    dynarray_reset(da);
}

void type_free_r(type *t)
{
    uptype_free(&t->up);
}

void type_free_dynarray_r(dynarray *ar)
{
    size_t i;
    dynarray_iter(ar, i){
        type *ent = dynarray_ent(ar, i);
        type_free_r(ent);
    }
    /* no reset */
}
*/
