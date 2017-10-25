#ifndef VARIABLE_STRUCT_H
#define VARIABLE_STRUCT_H

#include "type.h"
#include "init.h"

struct variable
{
	char *name, *name_mangled;
	type *ty;
};

struct variable_global
{
	struct variable var;
	struct init_toplvl *init;
};

#endif
