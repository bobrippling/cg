#include <stdio.h>
#include <ctype.h>

#include "string.h"

int dump_escaped_string(const struct string *str, FILE *f)
{
	size_t i;
	int ret = 0;

	for(i = 0; i < str->len; i++){
		char ch = str->str[i];
		int r;

		if(ch == '"')
			r = fprintf(f, "\\\"");
		else if(isprint(ch))
			r = fprintf(f, "%c", ch);
		else
			r = fprintf(f, "\\%03o", (unsigned)ch);

		if(r < 0)
			return r;
		ret += r;
	}

	return ret;
}
