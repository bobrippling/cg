#ifndef ISN_REG_H
#define ISN_REG_H

#include "backend_traits.h"

struct block;

struct regalloc_info
{
	struct backend_traits backend;
	struct uniq_type_list *uniq_type_list;
	struct function *func;
};

void regalloc(struct block *, struct regalloc_info *);

#endif
