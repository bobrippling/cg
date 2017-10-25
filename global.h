#ifndef GLOBAL_H
#define GLOBAL_H

#include <stdio.h>
#include "function.h"
#include "variable_global.h"

struct unit;
struct uniq_type_list;

typedef struct global global;

void global_dump(struct unit *, global *, FILE *);

const char *global_name(global *);
const char *global_name_mangled(global *, const struct target *);
struct type *global_type_as_ptr(struct uniq_type_list *, global *);
struct type *global_type_noptr(global *);

bool global_is_forward_decl(global *);

#endif
