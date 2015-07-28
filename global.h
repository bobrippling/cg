#ifndef GLOBAL_H
#define GLOBAL_H

#include "function.h"
#include "variable.h"

struct unit;
struct uniq_type_list;

typedef struct global global;

void global_dump(struct unit *, global *);

const char *global_name(global *);
struct type *global_type_as_ptr(struct uniq_type_list *, global *);
struct type *global_type_noptr(global *);

#endif
