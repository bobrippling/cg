#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "mem.h"

void *xcalloc(size_t n, size_t sz)
{
	void *p;
	if(n == 0)
		n = 1;
	p = calloc(n, sz);
	assert(p);
	return p;
}

void *xmalloc(size_t l)
{
	void *p;
	if(l == 0)
		l = 1;
	p = malloc(l);
	assert(p);
	return p;
}

void *xrealloc(void *p, size_t l)
{
	void *r = realloc(p, l);
	assert(r || l == 0);
	return r;
}

char *xstrdup(const char *s)
{
	size_t l = strlen(s) + 1;
	char *new = xmalloc(l);
	memcpy(new, s, l);
	return new;
}
