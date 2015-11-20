#include <stddef.h>
#include <stdio.h>

#include "global.h"
#include "global_struct.h"

#include "type.h"

void global_dump(struct unit *unit, global *glob)
{
	const char *name;
	type *ty;

	(void)unit;

	if(glob->is_fn){
		name = function_name(glob->u.fn);
		ty = type_func_call(function_type(glob->u.fn), NULL);
	}else{
		variable *v = variable_global_var(glob->u.var);

		name = variable_name(v);
		ty = variable_type(v);
	}

	printf("$%s = %s", name, type_to_str(ty));

	if(glob->is_fn){
		function_dump_args_and_block(glob->u.fn);
	}else{
		printf("\n");
	}
}

const char *global_name(global *g)
{
	return g->is_fn
		? function_name(g->u.fn)
		: variable_name(variable_global_var(g->u.var));
}

struct type *global_type_noptr(global *g)
{
	return g->is_fn
		? function_type(g->u.fn)
		: variable_type(variable_global_var(g->u.var));
}

struct type *global_type_as_ptr(struct uniq_type_list *us, global *g)
{
	return type_get_ptr(us, global_type_noptr(g));
}
