#include <stddef.h>
#include <assert.h>

#include "regset.h"
#include "macros.h"

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

static unsigned regset_index(const struct regset *rs, unsigned index, int is_fp)
{
	size_t i;

	for(i = 0; i < rs->count; i++){
		if(is_fp == regt_is_fp(rs->regs[i])){
			if(index == 0)
				return i;
			index--;
		}
	}

	assert(0 && "register index out of bounds");
	return -1;
}

regt regset_nth(const struct regset *rs, unsigned index, int is_fp)
{
	return rs->regs[regset_index(rs, index, is_fp)];
}

static_assert(sizeof(regset_marks) * 8 >= 32, space_for_regset_marks);

void regset_mark(regset_marks *marks, regt reg, bool mark)
{
	/* enough space for 16 ints and 16 floats */
	assert(regt_index(reg) < 16);

	if(mark)
		*marks |= 1 << (regt_index(reg) * 2 + regt_is_fp(reg));
	else
		*marks &= ~(1 << (regt_index(reg) * 2 + regt_is_fp(reg)));
}

bool regset_is_marked(regset_marks marks, regt reg)
{
	return marks & (1 << (regt_index(reg) * 2 + regt_is_fp(reg)));
}
