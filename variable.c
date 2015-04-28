#include <stdio.h>
#include <stdlib.h>

#include "mem.h"

#include "variable.h"
#include "variable_internal.h"

struct variable
{
	char *name;
	unsigned sz;
};

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

void variable_dump(variable *v)
{
	printf("%u %s;\n",
			v->sz,
			variable_name(v));
}
