#ifndef BLK_REG_H
#define BLK_REG_H

#include "block.h"
#include "backend_traits.h"

struct regalloc_context
{
	struct backend_traits backend;
	struct uniq_type_list *uniq_type_list;
	struct function *func;
};

void blk_regalloc(block *, struct regalloc_context *);

void blk_lifecheck(block *);

#endif
