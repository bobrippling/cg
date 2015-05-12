#ifndef VARIABLE_H
#define VARIABLE_H

typedef struct variable variable;

void variable_free(variable *);

const char *variable_name(variable *);

unsigned variable_size(variable *, unsigned ptrsz);
void variable_size_align(
		variable *, unsigned ptrsz,
		unsigned *sz, unsigned *align);

void variable_dump(variable *, const char *post);

#endif
