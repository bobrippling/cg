#ifndef ISN_REG_H
#define ISN_REG_H

#include "isn_internal.h"
#include "block.h"
#include "backend_traits.h"
#include "function.h"

void isn_regalloc(block *blk, function *, const struct backend_traits *);

#endif
