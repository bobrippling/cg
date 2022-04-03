#include <stdlib.h>
#include <assert.h>

#include "mem.h"

#include "regset.h"
#include "regset_marks.h"

regset_marks regset_marks_new()
{
	/* first entry stores count */
	regset_marks m = xcalloc(REGSET_MARK_MAX, sizeof(*m));
	return m;
}

void regset_marks_free(regset_marks marks)
{
	free(marks);
}

static regset_marks regset_mark_pos(regset_marks marks, regt reg)
{
	size_t i = regt_index(reg) * 2 + regt_is_fp(reg);
	assert(i < REGSET_MARK_MAX);
	return &marks[i];
}

void regset_mark(regset_marks marks, regt reg, bool mark)
{
	unsigned char *p = regset_mark_pos(marks, reg);
	*p += mark ? 1 : -1;
}

unsigned char regset_mark_count(regset_marks marks, regt reg)
{
	return *regset_mark_pos(marks, reg);
}

bool regset_is_marked(regset_marks marks, regt reg)
{
	return regset_mark_count(marks, reg) > 0;
}
