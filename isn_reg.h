#ifndef ISN_REG_H
#define ISN_REG_H

#include "isn_internal.h"
#include "block.h"
#include "backend_traits.h"

void isn_regalloc(block *blk, const struct backend_traits *);

#endif
