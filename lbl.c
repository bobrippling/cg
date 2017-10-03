#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mem.h"
#include "lbl.h"

char *lbl_new_private(unsigned *const counter, const char *prefix)
{
	const int n = strlen(prefix) + 32;
	char *l = xmalloc(n);

	int printed = xsnprintf(l, n, "%s%u", prefix, *counter);

	assert(printed < n);

	++*counter;

	return l;
}

char *lbl_new_ident(const char *ident, const char *prefix)
{
	const int n = strlen(ident) + strlen(prefix) + 2;
	char *l = xmalloc(n);

	int printed = xsnprintf(l, n, "%s_%s", prefix, ident);

	assert(printed < n);

	return l;
}

bool lbl_equal_to_ident(const char *lbl, const char *ident, const char *prefix)
{
	size_t prefix_len = strlen(prefix);

	if(strncmp(lbl, prefix, prefix_len))
		return false;

	if(lbl[prefix_len] != '_')
		return false;

	if(strcmp(lbl + prefix_len + 1, ident))
		return false;

	return true;
}
