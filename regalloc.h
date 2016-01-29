#ifndef ISN_REG_H
#define ISN_REG_H

#include "backend_traits.h"

struct block;

struct regalloc_info
{
	struct backend_traits backend;
	struct function *func;
};

void regalloc(struct block *, struct regalloc_info *, unsigned *alloca);

#endif
