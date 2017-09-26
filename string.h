#ifndef STRING_H
#define STRING_H

#include <stdio.h>

struct string
{
	char *str;
	size_t len;
};

int dump_escaped_string(const struct string *, FILE *);

#endif
