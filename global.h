#ifndef GLOBAL_H
#define GLOBAL_H

#include "function.h"
#include "variable.h"

typedef struct global
{
	int is_fn;
	union
	{
		function *fn;
		variable *var;
	} u;
} global;

void global_dump(global *);

#endif
