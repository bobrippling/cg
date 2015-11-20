#ifndef VARIABLE_INTERNAL_H
#define VARIABLE_INTERNAL_H

struct type;

variable *variable_new(const char *, struct type *);
variable_global *variable_global_new(const char *, struct type *);

#endif
