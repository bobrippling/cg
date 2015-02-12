#ifndef ISN_INTERNAL_H
#define ISN_INTERNAL_H

#include "val_internal.h"

typedef struct isn isn;

isn *isn_head(void);

void isn_on_vals(isn *, void (val *, isn *, void *), void *);

#endif
