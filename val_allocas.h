#ifndef VAL_ALLOCAS_H
#define VAL_ALLOCAS_H

#include "val.h"

val *val_alloca_idx_get(val *lval, unsigned idx);
void val_alloca_idx_set(val *lval, unsigned idx, val *elemptr);

#endif
