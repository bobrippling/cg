#ifndef VAL_INTERNAL_H
#define VAL_INTERNAL_H

#include <stdbool.h>
#include "val.h"
#include "op.h"

bool val_op_maybe(enum op, val *, val *, int *res);
bool val_op_maybe_val(enum op, val *, val *, val **res);
val *val_op_symbolic(enum op, val *, val *);

enum val_to
{
	LITERAL  = 1 << 0,
	NAMED = 1 << 1,
	ADDRESSABLE = 1 << 2,
};

val *val_need(val *v, enum val_to to, const char *from);
#define VAL_NEED(v, t) val_need(v, t, __func__)

int val_size(val *);

#endif
