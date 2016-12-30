#include <stdio.h>
#include <stdlib.h>

#include "mem.h"

#include "variable.h"
#include "variable_global.h"
#include "variable_internal.h"
#include "variable_struct.h"

static void variable_new_at(variable *v, char *name, struct type *ty)
{
	v->name = name;
	v->ty = ty;
}

variable_global *variable_global_new(char *name, struct type *ty)
{
	variable_global *v = xmalloc(sizeof *v);
	variable_new_at(&v->var, name, ty);
	v->init = NULL;
	return v;
}

void variable_deinit(variable *v)
{
	free(v->name);
}

void variable_free(variable *v)
{
	variable_deinit(v);
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
