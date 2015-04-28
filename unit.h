#ifndef UNIT_H
#define UNIT_H

#include "global.h"

typedef struct unit unit;

unit *unit_new(void);
void unit_free(unit *);

void unit_on_functions(unit *, void (function *));
void unit_on_globals(unit *, void (global *));

function *unit_function_new(unit *u, const char *lbl, unsigned retsz);

variable *unit_variable_new(unit *u, const char *lbl, unsigned sz);

#endif
