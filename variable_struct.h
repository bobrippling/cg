#ifndef VARIABLE_STRUCT_H
#define VARIABLE_STRUCT_H

#include "type.h"
#include "init.h"

struct variable
{
	char *name;
	type *ty;
};

struct variable_global
{
	struct variable var;
	struct init *init;
};

#endif
