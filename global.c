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

	switch(glob->kind){
		case GLOBAL_FUNC:
			name = function_name(glob->u.fn);
			ty = type_func_call(function_type(glob->u.fn), NULL, NULL);
			break;

		case GLOBAL_VAR:
		{
			variable *v = variable_global_var(glob->u.var);

			name = variable_name(v);
			ty = variable_type(v);

			break;
		}

		case GLOBAL_TYPE:
			printf("type ");
			name = type_alias_name(glob->u.ty);
			ty = type_alias_resolve(glob->u.ty);
	}

	printf("$%s = %s", name, type_to_str(ty));

	switch(glob->kind){
		case GLOBAL_FUNC:
			function_dump_args_and_block(glob->u.fn);
			break;

		case GLOBAL_VAR:
		{
			struct init_toplvl *init = variable_global_init(glob->u.var);

			if(init){
				putchar(' ');
				init_dump(init);
			}

			printf("\n");
			break;
		}

		case GLOBAL_TYPE:
			printf("\n");
			break;
	}
}

const char *global_name(global *g)
{
	switch(g->kind){
		case GLOBAL_FUNC:
			return function_name(g->u.fn);

		case GLOBAL_VAR:
			return variable_name(variable_global_var(g->u.var));

		case GLOBAL_TYPE:
			return type_alias_name(g->u.ty);
	}
}

struct type *global_type_noptr(global *g)
{
	switch(g->kind){
		case GLOBAL_FUNC:
			return function_type(g->u.fn);

		case GLOBAL_VAR:
			return variable_type(variable_global_var(g->u.var));

		case GLOBAL_TYPE:
			return g->u.ty;
	}
}

struct type *global_type_as_ptr(struct uniq_type_list *us, global *g)
{
	return type_get_ptr(us, global_type_noptr(g));
}

bool global_is_forward_decl(global *g)
{
	switch(g->kind){
		case GLOBAL_FUNC:
			return function_is_forward_decl(g->u.fn);

		case GLOBAL_VAR:
			return variable_global_is_forward_decl(g->u.var);

		case GLOBAL_TYPE:
			return true;
	}
}
