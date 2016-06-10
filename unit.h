#ifndef UNIT_H
#define UNIT_H

#include "global.h"

struct type;
struct uniq_type_list;
struct dynarray;
struct target;

typedef struct unit unit;

typedef void global_emit_func(unit *, global *);

unit *unit_new(const struct target *);
void unit_free(unit *);

void unit_on_functions(unit *, void (function *, void *), void *);
void unit_on_globals(unit *, global_emit_func);

function *unit_function_new(
		unit *u, char *lbl /*consumed*/,
		struct type *fnty, struct dynarray *toplvl_args);

variable_global *unit_variable_new(unit *u, char *lbl, struct type *ty);

void unit_type_new(unit *u, struct type *alias);


global *unit_global_find(unit *, const char *);

struct uniq_type_list *unit_uniqtypes(unit *);
const struct target *unit_target_info(unit *);

void unit_dump(unit *);

#endif
