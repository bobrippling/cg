#ifndef MEM_H
#define MEM_H

#include <stddef.h>

void *xmalloc(size_t);
void *xcalloc(size_t n, size_t sz);
char *xstrdup(const char *);

#endif
