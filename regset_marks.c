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
	unsigned char *const ent = regset_mark_pos(marks, reg);

	if(mark){
		++*ent;
	}else{
		assert(*ent > 0);
		--*ent;
	}
}

bool regset_is_marked(regset_marks marks, regt reg)
{
	return !!*regset_mark_pos(marks, reg);
}
