#ifndef MEM_H
#define MEM_H

#include <stddef.h>

void *xmalloc(size_t);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *, size_t);
char *xstrdup(const char *);

#endif
