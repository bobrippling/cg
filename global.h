#ifndef GLOBAL_H
#define GLOBAL_H

#include "function.h"
#include "variable.h"

typedef struct global global;

void global_dump(global *);

const char *global_name(global *);
struct type *global_type(global *);

#endif
