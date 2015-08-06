#ifndef ISN_H
#define ISN_H

#include "op.h"
#include "block.h"
#include "dynarray.h"

void isn_load(block *, struct val *to, struct val *lval);
void isn_store(block *, struct val *from, struct val *lval);

void isn_alloca(block *, struct val *);

void isn_elem(block *, struct val *lval, struct val *index, struct val *res);

void isn_op(block *, enum op op, struct val *lhs, struct val *rhs, struct val *res);
void isn_cmp(block *, enum op_cmp, struct val *lhs, struct val *rhs, struct val *res);

void isn_zext(block *, struct val *, struct val *);

void isn_br(block *, struct val *cond, block *btrue, block *bfalse);
void isn_jmp(block *, block *);

void isn_ret(block *, struct val *);

void isn_call(block *, struct val *into, struct val *fn, dynarray *args /*not consumed*/);

#endif
