#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include "mem.h"
#include "macros.h"

#include "type.h"
#include "type_uniq_struct.h"
#include "strbuf_fixed.h"

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
			struct uniq_type_list *uniqs;
		} func;
		struct
		{
			dynarray membs;
		} struct_;
	} u;

	enum type_kind
	{
		PRIMITIVE, PTR, ARRAY, STRUCT, FUNC, VOID
	} kind;
};

bool type_is_fn(type *t)
{
	return t && t->kind == FUNC;
}

bool type_is_struct(type *t)
{
	return t && t->kind == STRUCT;
}

bool type_is_primitive(type *t, enum type_primitive prim)
{
	if(!t)
		return false;

	if(t->kind != PRIMITIVE)
		return false;
	return t->u.prim == prim;
}

bool type_is_int(type *t)
{
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

bool type_is_void(type *t)
{
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

static bool type_to_strbuf(strbuf_fixed *const buf, type *t)
{
	switch(t->kind){
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

type *type_get_func(uniq_type_list *us, type *ret, /*consumed*/dynarray *args)
{
	size_t i;
	type *func;

	dynarray_iter(&ret->up.funcs, i){
		type *ent = dynarray_ent(&ret->up.funcs, i);

		assert(ent->kind == FUNC);
		if(ent->u.func.ret == ret && dynarray_refeq(&ent->u.func.args, args))
			return ent;
	}

	func = tnew(FUNC);
	memset(&func->u.func.args, 0, sizeof func->u.func.args);
	dynarray_move(&func->u.func.args, args);
	func->u.func.ret = ret;
	func->u.func.uniqs = us;

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
	if(!t)
		return NULL;

	if(t->kind != PTR)
		return NULL;

	return t->u.ptr.pointee;
}

type *type_func_call(type *t, dynarray **const args)
{
	if(!t)
		return NULL;

	if(t->kind != FUNC)
		return NULL;

	if(args){
		*args = &t->u.func.args;
	}

	return t->u.func.ret;
}

dynarray *type_func_args(type *t)
{
	dynarray *args;
	if(!t)
		return NULL;

	type_func_call(t, &args);
	return args;
}

type *type_array_element(type *t)
{
	if(!t)
		return NULL;

	if(t->kind != ARRAY)
		return NULL;

	return t->u.array.elem;
}

type *type_struct_element(type *t, size_t i)
{
	size_t n;

	if(!t)
		return NULL;

	if(t->kind != STRUCT)
		return NULL;

	n = dynarray_count(&t->u.struct_.membs);

	if(i >= n)
		return NULL;

	return dynarray_ent(&t->u.struct_.membs, i);
}

void type_size_align(type *ty, unsigned *sz, unsigned *align)
{
	switch(ty->kind){
		case PRIMITIVE:
			type_primitive_size_align(ty->u.prim, sz, align);
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
				type *ent = dynarray_ent(&ty->u.struct_.membs, i);

				type_size_align(ent, &elemsz, &elemalign);

				*sz += elemsz;
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
			assert(0 && "size of void");
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
