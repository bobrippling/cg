#ifndef LOCATION_H
#define LOCATION_H

#include <stdbool.h>

#include "reg.h"

struct location
{
	enum
	{
		NAME_NOWHERE,
		NAME_IN_REG,
		NAME_SPILT
	} where;
	union
	{
		regt reg;
		int off;
	} u;
};
#define location_init_reg(l) ((l)->where = NAME_NOWHERE, (l)->u.reg = regt_make_invalid())
unsigned location_hash(struct location const *);
bool location_eq(struct location const *, struct location const *);

#endif
