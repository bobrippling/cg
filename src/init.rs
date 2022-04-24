use bitflags::bitflags;

use crate::ty::Type;

#[derive(Debug, PartialEq, Eq)]
pub enum Init<'arena> {
    Str(Vec<u8>),
    Array(Vec<Init<'arena>>),
    Struct(Vec<Init<'arena>>),
    Ptr(PtrInit),
    Int(usize),
    Alias { as_: Type<'arena>, init: Box<Init<'arena>> },
}

#[derive(Debug, PartialEq, Eq)]
pub enum PtrInit {
    Int(usize),
    Label {
        label: String,
        offset: isize,
        is_anyptr: bool,
    },
}

bitflags! {
    #[derive(Default)]
    pub struct InitFlags: u8 {
        const INTERNAL = 1 << 0;
        const CONSTANT = 1 << 1;
        const WEAK = 1 << 2;
    }
}

#[derive(Debug, PartialEq, Eq)]
pub struct InitTopLevel<'arena> {
    pub init: Init<'arena>,
    pub flags: InitFlags,
}

/*
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include "init.h"
#include "type.h"

static void init_dump_r(struct init *init, FILE *f)
{
    switch(init->type){
        case init_int:
            fprintf(f, "%#llx", init->u.i);
            break;

        case init_alias:
            fprintf(f, "aliasinit %s ", type_to_str(init->u.alias.as));
            init_dump_r(init->u.alias.init, f);
            break;

        case init_str:
            fputc('\"', f);
            dump_escaped_string(&init->u.str, f);
            fputc('\"', f);
            break;

        case init_array:
        case init_struct:
        {
            size_t i;
            const char *comma = "";

            fprintf(f, "{ ");

            dynarray_iter(&init->u.elem_inits, i){
                fprintf(f, "%s", comma);
                init_dump_r(dynarray_ent(&init->u.elem_inits, i), f);
                comma = ", ";
            }
            fprintf(f, " }");

            break;
        }

        case init_ptr:
        {
            if(init->u.ptr.is_label){
                long off = init->u.ptr.u.ident.label.offset;

                fprintf(f, "$%s %s %ld%s",
                        init->u.ptr.u.ident.label.ident,
                        off > 0 ? "add" : "sub",
                        off > 0 ? off : -off,
                        init->u.ptr.u.ident.is_anyptr ? " anyptr" : "");
            }else{
                fprintf(f, "%lu", init->u.ptr.u.integral);
            }
            break;
        }
    }
}

void init_dump(struct init_toplvl *init, FILE *f)
{
    fprintf(f, "%s ", init->internal ? "internal" : "global");

    if(init->constant)
        fprintf(f, "const ");
    if(init->weak)
        fprintf(f, "weak ");

    init_dump_r(init->init, f);
}

static void init_free_r(struct init *init)
{
    switch(init->type){
        case init_str:
            free(init->u.str.str);
            break;

        case init_array:
        case init_struct:
        {
            size_t i;
            dynarray_iter(&init->u.elem_inits, i){
                struct init *sub = dynarray_ent(&init->u.elem_inits, i);
                init_free_r(sub);
            }
            dynarray_reset(&init->u.elem_inits);
            break;
        }

        case init_ptr:
            if(init->u.ptr.is_label){
                free(init->u.ptr.u.ident.label.ident);
            }
            break;

        case init_int:
            break;

        case init_alias:
            init_free_r(init->u.alias.init);
            break;
    }
    free(init);
}

void init_free(struct init_toplvl *init)
{
    if(!init)
        return;
    init_free_r(init->init);
    free(init);
}
*/
