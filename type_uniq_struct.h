#ifndef TYPE_UNIQ_STRUCT_H
#define TYPE_UNIQ_STRUCT_H

#include "type.h"

struct uniq_type_list
{
	type *primitives[TYPE_PRIMITIVE_LAST];
	type *tvoid;

	unsigned ptrsz, ptralign;
};

#endif
