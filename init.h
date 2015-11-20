#ifndef INIT_H
#define INIT_H

#include "dynarray.h"
#include "string.h"
#include "label.h"

struct init
{
	enum {
		init_str,
		init_array,
		init_struct,
		init_ptr,
		init_int
	} type;

	union
	{
		struct string str;
		dynarray elem_inits; /* array and struct */
		struct label_off ptr;
		unsigned long long i;
	} u;
};

#endif
