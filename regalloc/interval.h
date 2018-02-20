#ifndef INTERVAL_H
#define INTERVAL_H

#include "../val.h"
#include "../location.h"

typedef struct interval
{
	val *val;
	struct location *loc;
	unsigned start, end;
	struct isn *start_isn;

	dynarray freeregs;
	unsigned regspace;
}	interval;

#endif
