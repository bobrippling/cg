#ifndef STACK_H
#define STACK_H

struct type;
struct function;

void lsra_stackalloc(struct location *loc, struct function *fn, struct type *ty);

#endif
