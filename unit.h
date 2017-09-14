#ifndef UNIT_H
#define UNIT_H

#include "global.h"

struct type;
struct uniq_type_list;
struct dynarray;
struct target;

typedef struct unit unit;

typedef void on_global_func(unit *, global *, void *);

unit *unit_new(const struct target *);
void unit_free(unit *);

void unit_on_functions(unit *, void (function *, unit *, void *), void *);
void unit_on_globals(unit *, on_global_func, void *);

function *unit_function_new(
		unit *u, char *lbl /*consumed*/,
		struct type *fnty, struct dynarray *toplvl_args);

variable_global *unit_variable_new(unit *u, char *lbl, struct type *ty);

void unit_type_new(unit *u, struct type *alias);


global *unit_global_find(unit *, const char *);

struct uniq_type_list *unit_uniqtypes(unit *);
const struct target *unit_target_info(unit *);

#endif
