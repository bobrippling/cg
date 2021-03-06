#include <stdlib.h>

#include "variable_global.h"
#include "variable_struct.h"
#include "init.h"

variable *variable_global_var(variable_global *g)
{
	return &g->var;
}

void variable_global_init_set(variable_global *g, struct init_toplvl *init)
{
	g->init = init;
}

struct init_toplvl *variable_global_init(variable_global *g)
{
	return g->init;
}

bool variable_global_is_forward_decl(variable_global *g)
{
	return !g->init;
}

void variable_global_free(variable_global *v)
{
	variable_deinit(&v->var);
	init_free(v->init);
	free(v);
}
