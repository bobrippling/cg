#include <stdlib.h>
#include <string.h>

#include "mem.h"
#include "unit.h"
#include "unit_internal.h"
#include "target.h"

#include "variable_internal.h"
#include "function_internal.h"
#include "type_uniq_struct.h"
#include "global_struct.h"

struct unit
{
	struct uniq_type_list types;
	const struct target *target_info;

	global **globals;
	size_t nglobals;

	unsigned uniq_counter;
};

static void uniq_types_init(
		struct uniq_type_list *us, unsigned ptrsz, unsigned ptralign)
{
	us->ptrsz = ptrsz;
	us->ptralign = ptralign;
}

unit *unit_new(const struct target *target)
{
	unit *u = xcalloc(1, sizeof *u);

	u->target_info = target;
	uniq_types_init(&u->types, target->arch.ptr.size, target->arch.ptr.align);

	return u;
}

uniq_type_list *unit_uniqtypes(unit *u)
{
	return &u->types;
}

const struct target *unit_target_info(unit *u)
{
	return u->target_info;
}

const char *unit_lbl_private_prefix(unit *u)
{
	return u->target_info->sys.lbl_priv_prefix;
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
	struct global *g;

	u->nglobals++;
	u->globals = xrealloc(u->globals, u->nglobals * sizeof *u->globals);

	g = xmalloc(sizeof *g);
	u->globals[u->nglobals - 1] = g;

	g->is_fn = is_fn;
	if(is_fn)
		g->u.fn = global;
	else
		g->u.var = global;
}

function *unit_function_new(
		unit *u, const char *lbl,
		struct type *fnty, struct dynarray *toplvl_args)
{
	function *fn = function_new(lbl, fnty, toplvl_args, &u->uniq_counter);

	unit_add_global(u, fn, 1);

	return fn;
}

variable_global *unit_variable_new(unit *u, const char *lbl, struct type *ty)
{
	variable_global *var = variable_global_new(lbl, ty);

	unit_add_global(u, var, 0);

	return var;
}

global *unit_global_find(unit *u, const char *spel)
{
	size_t i;

	for(i = 0; i < u->nglobals; i++){
		const char *sp = global_name(u->globals[i]);

		if(!strcmp(sp, spel))
			return u->globals[i];
	}

	return NULL;
}
