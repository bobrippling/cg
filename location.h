#ifndef LOCATION_H
#define LOCATION_H

#include <stdbool.h>

#include "reg.h"

struct location
{
	enum
	{
		NAME_NOWHERE, /* incomplete */
		NAME_IN_REG_ANY, /* partially complete */
		NAME_IN_REG,
		NAME_SPILT
	} where : 16;
	enum
	{
		NAME_CONSTRAIN_NONE,
		NAME_CONSTRAIN_REG,
		NAME_CONSTRAIN_STACK
	} constrain : 16; /* tracking metadata used for regalloc validation */
	union
	{
		regt reg;
		int off;
	} u;
};
#define location_init_reg(l) (\
		(l)->where = NAME_NOWHERE, \
		(l)->constrain = NAME_CONSTRAIN_NONE, \
		(l)->u.reg = regt_make_invalid()\
	)

unsigned location_hash(struct location const *);
bool location_eq(struct location const *, struct location const *);

#define location_is_reg(l) ((l) == NAME_IN_REG || (l) == NAME_IN_REG_ANY)

#endif
