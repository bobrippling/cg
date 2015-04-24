#ifndef UNIT_H
#define UNIT_H

#include "function.h"

typedef struct unit unit;

unit *unit_new(void);
void unit_free(unit *);

void unit_on_functions(unit *, void (function *));

function *unit_function_new(unit *u, const char *lbl, unsigned retsz);

#endif
