#include <stdio.h>
#include <assert.h>

#include "mem.h"
#include "lbl.h"

char *lbl_new(unsigned *const counter)
{
	int n = 32;
	char *l = xmalloc(n);

	int printed = snprintf(l, n, "L_%u", *counter);

	assert(printed < n);

	++*counter;

	return l;
}
