#ifndef LIFETIME_STRUCT_H
#define LIFETIME_STRUCT_H

#include <limits.h>

struct lifetime
{
	struct isn *start, *end;
};

#endif
