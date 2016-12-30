#ifndef VARIABLE_GLOBAL_H
#define VARIABLE_GLOBAL_H

#include "variable.h"
#include "init.h"

typedef struct variable_global variable_global;

variable *variable_global_var(variable_global *);

void variable_global_init_set(variable_global *, struct init_toplvl *);
struct init_toplvl *variable_global_init(variable_global *);

bool variable_global_is_forward_decl(variable_global *);

void variable_global_free(variable_global *);

#endif
