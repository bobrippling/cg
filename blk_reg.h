#ifndef BLK_REG_H
#define BLK_REG_H

#include "block.h"
#include "backend_traits.h"

void blk_regalloc(block *, struct backend_traits *);

void blk_lifecheck(block *);

#endif
