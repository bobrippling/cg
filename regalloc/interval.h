#ifndef INTERVAL_H
#define INTERVAL_H

#include "../val.h"
#include "../location.h"

typedef struct interval
{
	val *val;
	struct location *loc;
	unsigned start, end;

	dynarray freeregs;
	unsigned regspace;
}	interval;

#endif
