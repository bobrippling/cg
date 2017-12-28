#ifndef LOCATION_H
#define LOCATION_H

#include <stdbool.h>

#include "reg.h"

enum location_constraint
{
	CONSTRAINT_NONE  = 0,
	CONSTRAINT_REG   = 1 << 0,
	CONSTRAINT_CONST = 1 << 1,
	CONSTRAINT_MEM   = 1 << 2
};

struct location
{
	enum
	{
		NAME_NOWHERE, /* incomplete */
		NAME_IN_REG_ANY, /* partially complete */
		NAME_IN_REG,
		NAME_SPILT
	} where : 16;
	enum location_constraint constraint : 16; /* tracking metadata used for regalloc validation */
	union
	{
		regt reg;
		int off;
	} u;
};
#define location_init_reg(l) (\
		(l)->where = NAME_NOWHERE, \
		(l)->constraint = CONSTRAINT_NONE, \
		(l)->u.reg = regt_make_invalid()\
	)

unsigned location_hash(struct location const *);
bool location_eq(struct location const *, struct location const *);

#define location_is_reg(l) ((l) == NAME_IN_REG || (l) == NAME_IN_REG_ANY)
#define location_fully_allocated(l) ((l)->where == NAME_IN_REG || (l)->where == NAME_SPILT)

#endif
