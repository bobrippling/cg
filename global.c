#include <stddef.h>
#include <stdio.h>

#include "global.h"
#include "global_struct.h"

#include "type.h"

void global_dump(struct unit *unit, global *glob)
{
	const char *name;
	const char *desc;
	type *ty;

	(void)unit;

	if(glob->is_fn){
		name = function_name(glob->u.fn);
		desc = "func";
		ty = type_func_call(function_type(glob->u.fn), NULL);
	}else{
		name = variable_name(glob->u.var);
		desc = "data";
		ty = variable_type(glob->u.var);
	}

	printf("$%s = %s %s", name, desc, type_to_str(ty));

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
