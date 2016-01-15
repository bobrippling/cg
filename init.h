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
		struct
		{
			struct label_off label;
			int is_anyptr;
		} ptr;
		unsigned long long i;
	} u;
};

struct init_toplvl
{
	struct init *init;
	bool internal, constant, weak;
};

void init_dump(struct init_toplvl *);

#endif
