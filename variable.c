#include <stdio.h>
#include <stdlib.h>

#include "mem.h"

#include "variable.h"
#include "variable_internal.h"
#include "variable_struct.h"

variable *variable_new(const char *name, unsigned sz)
{
	variable *v = xmalloc(sizeof *v);

	v->name = xstrdup(name);
	v->sz = sz;

	return v;
}

void variable_free(variable *v)
{
	free(v->name);
	free(v);
}

const char *variable_name(variable *v)
{
	return v->name;
}

void variable_size_align(
		variable *v, unsigned ptrsz,
		unsigned *sz, unsigned *align)
{
	*sz = v->sz ? v->sz : ptrsz;
	*align = *sz; /* for now */
}

unsigned variable_size(variable *v, unsigned ptrsz)
{
	unsigned sz, align;
	variable_size_align(v, ptrsz, &sz, &align);
	return sz;
}

void variable_dump(variable *v, const char *post)
{
	printf("%u %s%s",
			v->sz,
			variable_name(v),
			post);
}
