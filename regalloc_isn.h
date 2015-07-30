#ifndef ISN_REG_H
#define ISN_REG_H

#include "isn_internal.h"
#include "block.h"
#include "backend_traits.h"
#include "function.h"

struct uniq_type_list;

void isn_regalloc(
		block *blk, function *,
		struct uniq_type_list *,
		const struct backend_traits *);

#endif
