#include "variable_global.h"
#include "variable_struct.h"

variable *variable_global_var(variable_global *g)
{
	return &g->var;
}

void variable_global_init_set(variable_global *g, struct init *init)
{
	g->init = init;
}
