#include <ctype.h>
#include <string.h>

#include "str.h"

int str_beginswith(const char *full, const char *prefix)
{
	return !strncmp(full, prefix, strlen(prefix));
}

int isident(char ch, int digit)
{
	if(ch == '_')
		return 1;
	return digit ? isalnum(ch) : isalpha(ch);
}
