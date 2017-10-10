#include <stddef.h>
#include <stdio.h>
#include <assert.h>

#include "global.h"
#include "global_struct.h"

#include "type.h"

void global_dump(struct unit *unit, global *glob, FILE *fout)
{
	const char *name;
	type *ty;

	(void)unit;

	switch(glob->kind){
		case GLOBAL_FUNC:
			function_dump(glob->u.fn, fout);
			return;

		case GLOBAL_VAR:
		{
			variable *v = variable_global_var(glob->u.var);

			name = variable_name(v);
			ty = variable_type(v);

			break;
		}

		case GLOBAL_TYPE:
			fprintf(fout, "type ");
			name = type_alias_name(glob->u.ty);
			ty = type_alias_resolve(glob->u.ty);
	}

	fprintf(fout, "$%s = %s", name, ty ? type_to_str(ty) : "");

	switch(glob->kind){
		case GLOBAL_FUNC:
			assert(0 && "unreachable");

		case GLOBAL_VAR:
		{
			struct init_toplvl *init = variable_global_init(glob->u.var);

			if(init){
				fputc(' ', fout);
				init_dump(init, fout);
			}

			fprintf(fout, "\n");
			break;
		}

		case GLOBAL_TYPE:
			fprintf(fout, "\n");
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
