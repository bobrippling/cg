#ifndef REGSET_H
#define REGSET_H

#include <stdbool.h>

#include "reg.h"

struct regset
{
	const regt *regs;
	unsigned count;
};

unsigned regset_int_count(const struct regset *);
unsigned regset_fp_count(const struct regset *);

regt regset_nth(const struct regset *, unsigned index, int is_fp);
#define regset_get(rs, i) ((rs)->regs[i])

typedef unsigned long regset_marks;

void regset_mark(regset_marks *, regt, bool mark);
bool regset_is_marked(regset_marks, regt);

#endif
