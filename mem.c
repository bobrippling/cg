#include <stdio.h>
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

char *xvsprintf(const char *fmt, va_list l)
{
	char *buf = NULL;
	size_t len = 16, ret;

	do{
		va_list lcp;

		len *= 2;
		buf = xrealloc(buf, len);

		va_copy(lcp, l);
		ret = vsnprintf(buf, len, fmt, lcp);
		va_end(lcp);

	}while(ret >= len);

	return buf;

}

char *xsprintf(const char *fmt, ...)
{
	va_list l;
	char *s;
	va_start(l, fmt);
	s = xvsprintf(fmt, l);
	va_end(l);
	return s;
}

int xsnprintf(char *buf, size_t len, const char *fmt, ...)
{
	va_list l;
	int desired_space;

	va_start(l, fmt);
	desired_space = vsnprintf(buf, len, fmt, l);
	va_end(l);

	if(desired_space < 0 || desired_space >= (int)len){
		fprintf(stderr, "snprintf() overflow\n");
		abort();
	}

	return desired_space;
}
