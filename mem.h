#ifndef MEM_H
#define MEM_H

#include <stddef.h>
#include <stdarg.h>

void *xmalloc(size_t);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *, size_t);
char *xstrdup(const char *);
char *xsprintf(const char *fmt, ...);
char *xvsprintf(const char *fmt, va_list);

int xsnprintf(char *buf, size_t len, const char *fmt, ...);

#endif
