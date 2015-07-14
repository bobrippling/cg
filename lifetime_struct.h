#ifndef LIFETIME_STRUCT_H
#define LIFETIME_STRUCT_H

#include <limits.h>

struct lifetime
{
	unsigned start, end;
};

#define LIFETIME_INIT_INF { 0, UINT_MAX }

#endif
