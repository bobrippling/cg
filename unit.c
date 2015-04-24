#include <stdlib.h>

#include "mem.h"
#include "unit.h"

struct unit
{
	function **funcs;
	size_t nfuncs;

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

	for(i = 0; i < u->nfuncs; i++)
		fn(u->funcs[i]);
}

static void unit_add_function(unit *u, function *f)
{
	u->nfuncs++;
	u->funcs = xrealloc(u->funcs, u->nfuncs * sizeof *u->funcs);

	u->funcs[u->nfuncs - 1] = f;
}

function *unit_function_new(unit *u, const char *lbl, unsigned retsz)
{
	function *fn = function_new(lbl, retsz, &u->uniq_counter);

	unit_add_function(u, fn);

	return fn;
}
