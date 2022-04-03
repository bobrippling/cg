#include <string.h>
#include <assert.h>

#include "location.h"

#include "mem.h"

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
		case NAME_IN_REG_ANY:
			return true;

		case NAME_IN_REG:
			return a->u.reg == b->u.reg;

		case NAME_SPILT:
			return a->u.off == b->u.off;
	}

	assert(false);
	return false;
}

const char *location_constraint_to_str(enum location_constraint c)
{
	static char buf[sizeof "REG|CONST|MEM|" + 1];

	xsnprintf(buf, sizeof(buf), "%s%s%s",
			c & CONSTRAINT_REG ? "REG|" : "",
			c & CONSTRAINT_CONST ? "CONST|" : "",
			c & CONSTRAINT_MEM ? "MEM|" : "");

	if(*buf)
		buf[strlen(buf)-1] = '\0';
	else
		strcpy(buf, "NONE");

	return buf;
}
