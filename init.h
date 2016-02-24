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
		init_int,
		init_alias
	} type;

	union
	{
		struct string str;
		dynarray elem_inits; /* array and struct */
		struct
		{
			bool is_label;
			union
			{
				struct
				{
					struct label_off label;
					bool is_anyptr;
				} ident;
				unsigned long integral;
			} u;
		} ptr;
		unsigned long long i;
		struct
		{
			struct type *as;
			struct init *init;
		} alias;
	} u;
};

struct init_toplvl
{
	struct init *init;
	bool internal, constant, weak;
};

void init_dump(struct init_toplvl *);

#endif
