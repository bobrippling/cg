#include <string.h>

#include "str.h"

int str_beginswith(const char *full, const char *prefix)
{
	return !strncmp(full, prefix, strlen(prefix));
}
