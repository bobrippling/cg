#ifndef VARIABLE_H
#define VARIABLE_H

typedef struct variable variable;
struct target;

void variable_free(variable *);
void variable_deinit(variable *);

const char *variable_name(variable *);
const char *variable_name_mangled(variable *, const struct target *);
struct type *variable_type(variable *);

unsigned variable_size(variable *);
void variable_size_align(variable *v, unsigned *sz, unsigned *align);

#endif
