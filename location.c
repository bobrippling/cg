#include <assert.h>

#include "location.h"

unsigned location_hash(struct location const *loc)
{
	return loc->where ^ regt_hash(loc->u.reg);
}

bool location_eq(struct location const *a, struct location const *b)
{
	if(a->where != b->where)
		return false;

	switch(a->where){
		case NAME_NOWHERE:
			return true;

		case NAME_IN_REG:
			return a->u.reg == b->u.reg;

		case NAME_SPILT:
			return a->u.off == b->u.off;
	}

	assert(false);
	return false;
}
