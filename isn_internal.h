#ifndef ISN_INTERNAL_H
#define ISN_INTERNAL_H

#include "val_internal.h"

typedef struct isn isn;

void isn_free_r(isn *);

void isn_on_vals(isn *, void (val *, isn *, void *), void *);

void isn_dump(isn *, block *);

#endif
