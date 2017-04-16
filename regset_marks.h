#ifndef REGSET_MARKS_H
#define REGSET_MARKS_H

#include <stdbool.h>

#include "reg.h"

#define REGSET_MARK_MAX 32

struct regset;

typedef unsigned char *regset_marks;

regset_marks regset_marks_new(void);
void regset_marks_free(regset_marks);

void regset_mark(regset_marks, regt, bool mark);
bool regset_is_marked(regset_marks, regt);

#endif
