#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "unit.h"
#include "unit_internal.h"

#include "variable_internal.h"
#include "function_internal.h"
#include "type_uniq_struct.h"
#include "global_struct.h"

struct unit
{
	struct uniq_type_list types;

	global **globals;
	size_t nglobals;

	const char *lbl_private_prefix;

	unsigned uniq_counter;
};

static void uniq_types_init(
		struct uniq_type_list *us, unsigned ptrsz, unsigned ptralign)
{
	us->ptrsz = ptrsz;
	us->ptralign = ptralign;
}

unit *unit_new(unsigned ptrsz, unsigned ptralign, const char *lbl_priv_prefix)
{
	unit *u = xcalloc(1, sizeof *u);

	u->lbl_private_prefix = lbl_priv_prefix;
	uniq_types_init(&u->types, ptrsz, ptralign);

	return u;
}

uniq_type_list *unit_uniqtypes(unit *u)
{
	return &u->types;
}

const char *unit_lbl_private_prefix(unit *u)
{
	return u->lbl_private_prefix;
}

static void unit_function_free(function *f, void *ctx)
{
	(void)ctx;
	function_free(f);
}

void unit_free(unit *unit)
{
	size_t i;

	unit_on_functions(unit, unit_function_free, NULL);

	for(i = 0; i < unit->nglobals; i++)
		free(unit->globals[i]);

	free(unit);
}

void unit_on_functions(unit *u, void fn(function *, void *), void *ctx)
{
	size_t i;

	for(i = 0; i < u->nglobals; i++)
		if(u->globals[i]->is_fn)
			fn(u->globals[i]->u.fn, ctx);
}

void unit_on_globals(unit *u, global_emit_func *fn)
{
	size_t i;

	for(i = 0; i < u->nglobals; i++)
		fn(u, u->globals[i]);
}

static void unit_add_global(unit *u, void *global, int is_fn)
{
	u->nglobals++;
	u->globals = xrealloc(u->globals, u->nglobals * sizeof *u->globals);

	u->globals[u->nglobals - 1] = xmalloc(sizeof *u->globals[u->nglobals]);

	u->globals[u->nglobals - 1]->is_fn = is_fn;
	if(is_fn)
		u->globals[u->nglobals - 1]->u.fn = global;
	else
		u->globals[u->nglobals - 1]->u.var = global;
}

function *unit_function_new(
		unit *u, const char *lbl,
		struct type *fnty, struct dynarray *toplvl_args)
{
	function *fn = function_new(lbl, fnty, toplvl_args, &u->uniq_counter);

	unit_add_global(u, fn, 1);

	return fn;
}

variable *unit_variable_new(unit *u, const char *lbl, struct type *ty)
{
	variable *var = variable_new(lbl, ty);

	unit_add_global(u, var, 0);

	return var;
}

global *unit_global_find(unit *u, const char *spel)
{
	size_t i;

	for(i = 0; i < u->nglobals; i++){
		const char *sp;
		if(u->globals[i]->is_fn)
			sp = function_name(u->globals[i]->u.fn);
		else
			sp = variable_name(u->globals[i]->u.var);

		if(!strcmp(sp, spel))
			return u->globals[i];
	}

	return NULL;
}
