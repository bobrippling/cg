#ifndef STRING_H
#define STRING_H

struct string
{
	char *str;
	size_t len;
};

int dump_escaped_string(const struct string *);

#endif
