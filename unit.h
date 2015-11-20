#ifndef UNIT_H
#define UNIT_H

#include "global.h"

struct type;
struct uniq_type_list;
struct dynarray;

typedef struct unit unit;

typedef void global_emit_func(unit *, global *);

unit *unit_new(unsigned ptrsz, unsigned ptralign, const char *lbl_priv_prefix);
void unit_free(unit *);

void unit_on_functions(unit *, void (function *, void *), void *);
void unit_on_globals(unit *, global_emit_func);

function *unit_function_new(
		unit *u, const char *lbl,
		struct type *fnty, struct dynarray *toplvl_args);

variable_global *unit_variable_new(
		unit *u, const char *lbl,
		struct type *ty);

global *unit_global_find(unit *, const char *);

struct uniq_type_list *unit_uniqtypes(unit *);

#endif
