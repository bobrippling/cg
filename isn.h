#ifndef ISN_H
#define ISN_H

#include "op.h"
#include "block.h"

void isn_load(block *, val *to, val *lval);
void isn_store(block *, val *from, val *lval);

void isn_alloca(block *, unsigned sz, val *);

void isn_op(block *, enum op op, val *lhs, val *rhs, val *res);
void isn_elem(block *, val *lval, val *add, val *res);

void isn_ret(block *, val *);

#endif
