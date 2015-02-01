#ifndef VAL_INTERNAL_H
#define VAL_INTERNAL_H

#include <stdbool.h>
#include "val.h"
#include "op.h"

bool val_op_maybe(enum op, val *, val *, int *res);
bool val_op_maybe_val(enum op, val *, val *, val **res);
val *val_op_symbolic(enum op, val *, val *);

#endif
