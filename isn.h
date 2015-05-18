#ifndef ISN_H
#define ISN_H

#include "op.h"
#include "block.h"
#include "dynarray.h"

void isn_load(block *, val *to, val *lval);
void isn_store(block *, val *from, val *lval);

void isn_alloca(block *, unsigned sz, val *);

void isn_elem(block *, val *lval, val *add, val *res);

void isn_op(block *, enum op op, val *lhs, val *rhs, val *res);
void isn_cmp(block *, enum op_cmp, val *lhs, val *rhs, val *res);

void isn_zext(block *, val *, val *);

void isn_br(block *, val *cond, block *btrue, block *bfalse);
void isn_jmp(block *, block *);

void isn_ret(block *, val *);

void isn_call(block *, val *into, val *fn, dynarray *args /*not consumed*/);

#endif
