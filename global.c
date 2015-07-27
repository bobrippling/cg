#include <stddef.h>
#include <stdio.h>

#include "global.h"
#include "global_struct.h"

void global_dump(global *glob)
{
	if(glob->is_fn)
		function_dump(glob->u.fn);
	else
		variable_dump(glob->u.var, ";\n");
}

const char *global_name(global *g)
{
	return g->is_fn
		? function_name(g->u.fn)
		: variable_name(g->u.var);
}

struct type *global_type(global *g)
{
	return g->is_fn
		? function_type(g->u.fn)
		: variable_type(g->u.var);
}
