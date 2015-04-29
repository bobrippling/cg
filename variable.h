#ifndef VARIABLE_H
#define VARIABLE_H

typedef struct variable variable;

void variable_free(variable *);

const char *variable_name(variable *);
unsigned variable_size(variable *);
void variable_dump(variable *);

#endif