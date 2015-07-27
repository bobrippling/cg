#ifndef UNIT_H
#define UNIT_H

#include "global.h"

struct type;
struct uniq_type_list;
struct dynarray;

typedef struct unit unit;

unit *unit_new(unsigned ptrsz, unsigned ptralign);
void unit_free(unit *);

void unit_on_functions(unit *, void (function *, void *), void *);
void unit_on_globals(unit *, void (global *));

function *unit_function_new(
		unit *u, const char *lbl,
		struct type *fnty, struct dynarray *toplvl_args);

variable *unit_variable_new(
		unit *u, const char *lbl,
		struct type *ty);

global *unit_global_find(unit *, const char *);

struct uniq_type_list *unit_uniqtypes(unit *);

#endif
