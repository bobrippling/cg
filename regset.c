#include <stddef.h>
#include <assert.h>

#include "regset.h"

static unsigned regset_count(const struct regset *rs, int fp)
{
	unsigned n = 0;
	size_t i;

	for(i = 0; i < rs->count; i++)
		n += (fp ? regt_is_fp(rs->regs[i]) : regt_is_int(rs->regs[i]));

	return n;
}

unsigned regset_int_count(const struct regset *rs)
{
	return regset_count(rs, 0);
}

unsigned regset_fp_count(const struct regset *rs)
{
	return regset_count(rs, 1);
}

regt regset_nth(const struct regset *rs, unsigned index, int is_fp)
{
	size_t i;

	for(i = 0; i < rs->count; i++){
		if(is_fp == regt_is_fp(rs->regs[i])){
			if(index == 0)
				return rs->regs[i];
			index--;
		}
	}

	assert(0 && "register index out of bounds");
	return -1;
}
