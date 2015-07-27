#ifndef TYPE_H
#define TYPE_H

#include <stdbool.h>
#include "dynarray.h"

typedef struct type type;
typedef struct uniq_type_list uniq_type_list;

#define TYPE_PRIMITIVES \
	X(i1, 1, 1)           \
	X(i2, 2, 2)           \
	X(i4, 4, 4)           \
	X(i8, 8, 8)           \
	X(f4, 4, 4)           \
	X(f8, 8, 8)           \
	X(flarge, 16, 16)

#define TYPE_PRIMITIVE_LAST flarge

enum type_primitive
{
#define X(name, sz, align) name,
	TYPE_PRIMITIVES
#undef X
};

bool type_is_fn(type *);

const char *type_to_str(type *);

/* --- getters --- */
type *type_get_void(uniq_type_list *);
type *type_get_primitive(uniq_type_list *, enum type_primitive);
type *type_get_ptr(uniq_type_list *, type *);
type *type_get_array(uniq_type_list *, type *, unsigned long);
type *type_get_func(uniq_type_list *, type *, /*consumed*/dynarray *);
type *type_get_struct(uniq_type_list *, dynarray *);


/* --- walkers --- */
type *type_deref(type *);
type *type_func_call(type *);


/* --- sizing --- */
void type_size_align(type *, unsigned *sz, unsigned *align);
bool type_size_to_primitive(unsigned, enum type_primitive *);
unsigned type_size(type *);

#endif
