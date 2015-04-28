#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "unit.h"

#include "variable_internal.h"
#include "function_internal.h"

struct unit
{
	global *globals;
	size_t nglobals;

	unsigned uniq_counter;
};

unit *unit_new(void)
{
	unit *u = xcalloc(1, sizeof *u);
	return u;
}

void unit_free(unit *unit)
{
	unit_on_functions(unit, function_free);

	free(unit);
}

void unit_on_functions(unit *u, void fn(function *))
{
	size_t i;

	for(i = 0; i < u->nglobals; i++)
		if(u->globals[i].is_fn)
			fn(u->globals[i].u.fn);
}

void unit_on_globals(unit *u, void fn(global *))
{
	size_t i;

	for(i = 0; i < u->nglobals; i++)
		fn(&u->globals[i]);
}

static void unit_add_global(unit *u, void *global, int is_fn)
{
	u->nglobals++;
	u->globals = xrealloc(u->globals, u->nglobals * sizeof *u->globals);

	u->globals[u->nglobals - 1].is_fn = is_fn;
	if(is_fn)
		u->globals[u->nglobals - 1].u.fn = global;
	else
		u->globals[u->nglobals - 1].u.var = global;
}

function *unit_function_new(unit *u, const char *lbl, unsigned retsz)
{
	function *fn = function_new(lbl, retsz, &u->uniq_counter);

	unit_add_global(u, fn, 1);

	return fn;
}

variable *unit_variable_new(unit *u, const char *lbl, unsigned sz)
{
	variable *var = variable_new(lbl, sz);

	unit_add_global(u, var, 0);

	return var;
}

global *unit_global_find(unit *u, const char *spel)
{
	size_t i;

	for(i = 0; i < u->nglobals; i++){
		const char *sp;
		if(u->globals[i].is_fn)
			sp = function_name(u->globals[i].u.fn);
		else
			sp = variable_name(u->globals[i].u.var);

		if(!strcmp(sp, spel))
			return &u->globals[i];
	}

	return NULL;
}
