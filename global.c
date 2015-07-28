#include <stddef.h>
#include <stdio.h>

#include "global.h"
#include "global_struct.h"

#include "type.h"

void global_dump(struct unit *unit, global *glob)
{
	(void)unit;
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

struct type *global_type_noptr(global *g)
{
	return g->is_fn
		? function_type(g->u.fn)
		: variable_type(g->u.var);
}

struct type *global_type_as_ptr(struct uniq_type_list *us, global *g)
{
	return type_get_ptr(us, global_type_noptr(g));
}
