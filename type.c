#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>

#include "mem.h"
#include "macros.h"

#include "type.h"
#include "type_uniq_struct.h"

#define TYBUF_SZ 256

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
	} u;

	enum type_kind
	{
		PRIMITIVE, PTR, ARRAY, FUNC, VOID
	} kind;
};

bool type_is_fn(type *t)
{
	return t->kind == FUNC;
}

bool type_is_primitive(type *t, enum type_primitive prim)
{
	if(t->kind != PRIMITIVE)
		return false;
	return t->u.prim == prim;
}

bool type_is_int(type *t)
{
	if(t->kind != PRIMITIVE)
		return false;
	switch(t->u.prim){
#define X(name, integral, sz, align) case name: return integral;
		TYPE_PRIMITIVES
#undef X
	}
	assert(0);
}

static const char *type_primitive_to_str(enum type_primitive p)
{
	switch(p){
		case i1: return "i1";
		case i2: return "i2";
		case i4: return "i4";
		case i8: return "i8";
		case f4: return "f4";
		case f8: return "f8";
		case flarge: return "flarge";
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

static bool tybuf_add(char tybuf[attr_static TYBUF_SZ], const char *fmt, ...)
{
	va_list args;
	size_t l = strlen(tybuf);

	if(l >= TYBUF_SZ){
		snprintf(tybuf, TYBUF_SZ, "<ty trunc>");
		return false;
	}

	va_start(args, fmt);
	vsnprintf(tybuf + l, TYBUF_SZ - l, fmt, args);
	va_end(args);

	return true;
}

const char *type_to_str(type *t)
{
	static char buf[TYBUF_SZ];

	switch(t->kind){
		case VOID:
			snprintf(buf, sizeof buf, "void");
			return buf;

		case PRIMITIVE:
			snprintf(buf, sizeof buf, "%s", type_primitive_to_str(t->u.prim));
			return buf;

		case PTR:
		{
			char *p = (char *)type_to_str(t->u.ptr.pointee);
			tybuf_add(p, "*");
			return p;
		}

		case ARRAY:
		{
			char *p = (char *)type_to_str(t->u.array.elem);
			tybuf_add(p, "[%lu]", t->u.array.n);
			return p;
		}

		case FUNC:
		{
			const char *comma = "";
			char *p = (char *)type_to_str(t->u.func.ret);
			size_t i;

			tybuf_add(p, "(");

			dynarray_iter(&t->u.func.args, i){
				if(!tybuf_add("%s%s", comma, t->u.array.n))
					break;

				comma = ", ";
			}

			tybuf_add(p, ")");

			return p;
		}
	}

	assert(0);
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
	/* TODO */
	(void)us;
	(void)membs;
	return NULL;
}


type *type_deref(type *t)
{
	if(t->kind != PTR)
		return NULL;

	return t->u.ptr.pointee;
}

type *type_func_call(type *t, dynarray **const args)
{
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
	type_func_call(t, &args);
	return args;
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

		case FUNC:
		{
			*sz = ty->u.func.uniqs->ptrsz;
			*align = ty->u.func.uniqs->ptralign;
			break;
		}

		case VOID:
		{
			*sz = *align = 1;
			break;
		}
	}
}

bool type_size_to_primitive(unsigned sz, enum type_primitive *p)
{
	switch(sz){
		case 1: *p = i1; return true;
		case 2: *p = i2; return true;
		case 4: *p = i4; return true;
		case 8: *p = i8; return true;
	}

	return false;
}

unsigned type_size(type *t)
{
	unsigned sz, align;
	type_size_align(t, &sz, &align);
	return sz;
}
