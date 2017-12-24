#ifndef INTERVAL_H
#define INTERVAL_H

#include "../val.h"
#include "../location.h"

typedef struct interval
{
	val *val;
	unsigned start, end;
	struct location *loc;
}	interval;

#endif
