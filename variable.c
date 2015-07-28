#include <stdio.h>
#include <stdlib.h>

#include "mem.h"

#include "variable.h"
#include "variable_internal.h"
#include "variable_struct.h"

variable *variable_new(const char *name, struct type *ty)
{
	variable *v = xmalloc(sizeof *v);

	v->name = xstrdup(name);
	v->ty = ty;

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

type *variable_type(variable *v)
{
	return v->ty;
}

void variable_size_align(variable *v, unsigned *sz, unsigned *align)
{
	type_size_align(v->ty, sz, align);
}

unsigned variable_size(variable *v)
{
	unsigned sz, align;
	variable_size_align(v, &sz, &align);
	return sz;
}
