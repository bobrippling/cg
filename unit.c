#include <stdlib.h>

#include "mem.h"
#include "unit.h"

struct unit
{
	function **funcs;
	size_t nfuncs;

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

	for(i = 0; i < u->nfuncs; i++)
		fn(u->funcs[i]);
}

void unit_add_function(unit *u, function *f)
{
	u->nfuncs++;
	u->funcs = xrealloc(u->funcs, u->nfuncs * sizeof *u->funcs);

	u->funcs[u->nfuncs - 1] = f;
}
