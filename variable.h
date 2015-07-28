#ifndef VARIABLE_H
#define VARIABLE_H

typedef struct variable variable;

void variable_free(variable *);

const char *variable_name(variable *);
struct type *variable_type(variable *);

unsigned variable_size(variable *);
void variable_size_align(variable *v, unsigned *sz, unsigned *align);

#endif
