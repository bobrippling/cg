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

unsigned variable_size(variable *v, unsigned ptrsz)
{
	return v->sz ? v->sz : ptrsz;
}

void variable_dump(variable *v, const char *post)
{
	printf("%u %s%s",
			v->sz,
			variable_name(v),
			post);
}
