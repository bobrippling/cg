#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "dynmap.h"

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

	dynmap *names2types;

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

void unit_free(unit *unit)
{
	size_t i;
	char *str;

	uniq_type_list_free(unit_uniqtypes(unit));

	for(i = 0; i < unit->nglobals; i++){
		global *g = unit->globals[i];

		switch(g->kind){
			case GLOBAL_FUNC:
				function_free(g->u.fn);
				break;
			case GLOBAL_VAR:
				variable_global_free(g->u.var);
				break;
			case GLOBAL_TYPE:
				break;
		}

		free(g);
	}

	for(i = 0; (str = dynmap_key(char *, unit->names2types, i)); i++)
		free(str);
	dynmap_free(unit->names2types);

	free(unit->globals);
	free(unit);
}

void unit_on_functions(unit *u, void fn(function *, void *), void *ctx)
{
	size_t i;

	for(i = 0; i < u->nglobals; i++)
		if(u->globals[i]->kind == GLOBAL_FUNC)
			fn(u->globals[i]->u.fn, ctx);
}

void unit_on_globals(unit *u, global_emit_func *fn)
{
	size_t i;

	for(i = 0; i < u->nglobals; i++)
		fn(u, u->globals[i]);
}

static void unit_add_global(unit *u, void *global, enum global_kind kind)
{
	struct global *g;

	u->nglobals++;
	u->globals = xrealloc(u->globals, u->nglobals * sizeof *u->globals);

	g = xmalloc(sizeof *g);
	u->globals[u->nglobals - 1] = g;

	g->kind = kind;
	switch(kind){
		case GLOBAL_FUNC:
			g->u.fn = global;
			break;
		case GLOBAL_VAR:
			g->u.var = global;
			break;
		case GLOBAL_TYPE:
			g->u.ty = global;
			break;
	}
}

function *unit_function_new(
		unit *u, char *lbl,
		struct type *fnty, struct dynarray *toplvl_args)
{
	function *fn = function_new(lbl, fnty, toplvl_args, &u->uniq_counter);

	unit_add_global(u, fn, GLOBAL_FUNC);

	return fn;
}

variable_global *unit_variable_new(unit *u, char *lbl, struct type *ty)
{
	variable_global *var = variable_global_new(lbl, ty);

	unit_add_global(u, var, GLOBAL_VAR);

	return var;
}

void unit_type_new(unit *u, type *alias)
{
	unit_add_global(u, alias, GLOBAL_TYPE);
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
