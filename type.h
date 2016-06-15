#ifndef TYPE_H
#define TYPE_H

#include <stdbool.h>
#include "dynarray.h"

typedef struct type type;
typedef struct uniq_type_list uniq_type_list;

#define TYPE_PRIMITIVES \
	X(i1, true, 1, 1)           \
	X(i2, true, 2, 2)           \
	X(i4, true, 4, 4)           \
	X(i8, true, 8, 8)           \
	X(f4, false, 4, 4)           \
	X(f8, false, 8, 8)           \
	X(flarge, false, 16, 16)

#define iMAX i8
#define TYPE_PRIMITIVE_LAST flarge

enum type_primitive
{
#define X(name, integral, sz, align) name,
	TYPE_PRIMITIVES
#undef X
};

void uniq_type_list_free(uniq_type_list *);

bool type_is_fn(type *);
bool type_is_fn_variadic(type *);
bool type_is_primitive(type *, enum type_primitive);
bool type_is_int(type *);
bool type_is_void(type *);
bool type_is_struct(type *);

const char *type_to_str(type *);
const char *type_to_str_r(char *buf, size_t buflen, type *t);

/* --- getters --- */
type *type_get_void(uniq_type_list *);
type *type_get_primitive(uniq_type_list *, enum type_primitive);
type *type_get_ptr(uniq_type_list *, type *);
type *type_get_array(uniq_type_list *, type *, unsigned long);
type *type_get_func(uniq_type_list *, type *, /*consumed*/dynarray *, bool variadic);
type *type_get_struct(uniq_type_list *, dynarray *);

/* --- aliases --- */
struct typealias *type_alias_add(uniq_type_list *, char * /* consumed */);
type *type_alias_complete(struct typealias *, type *);
type *type_alias_find(uniq_type_list *, const char *);
const char *type_alias_name(type *);
type *type_alias_resolve(type *);

/* --- walkers --- */
type *type_deref(type *);
type *type_func_call(type *, dynarray **, bool *);
dynarray *type_func_args(type *);
type *type_array_element(type *);
type *type_struct_element(type *, size_t);
size_t type_array_count(type *);

/* --- sizing --- */
void type_size_align(type *, unsigned *sz, unsigned *align);
unsigned type_size(type *);
unsigned type_align(type *);

/* --- uniq type list --- */
void uniq_types_init(
		struct uniq_type_list *us,
		unsigned ptrsz, unsigned ptralign);

#endif
