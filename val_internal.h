#ifndef VAL_INTERNAL_H
#define VAL_INTERNAL_H

#include <stdbool.h>
#include "val.h"
#include "op.h"

bool val_maybe_op(enum op, val *, val *, int *res);

#endif
