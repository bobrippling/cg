#ifndef REGSET_H
#define REGSET_H

#include "reg.h"

struct regset
{
	const regt *regs;
	unsigned count;
};

unsigned regset_int_count(const struct regset *);
unsigned regset_fp_count(const struct regset *);

regt regset_nth(const struct regset *, unsigned index, int is_fp);

#endif
