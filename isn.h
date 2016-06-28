#ifndef ISN_H
#define ISN_H

#include <stdbool.h>

#include "op.h"
#include "block.h"
#include "dynarray.h"

typedef struct isn isn;

isn *isn_load(struct val *to, struct val *lval);
isn *isn_store(struct val *from, struct val *lval);

isn *isn_alloca(struct val *);

isn *isn_elem(struct val *lval, struct val *index, struct val *res);
isn *isn_ptradd(struct val *lhs, struct val *rhs, struct val *out);

isn *isn_copy(struct val *to, struct val *from);

isn *isn_op(enum op op, struct val *lhs, struct val *rhs, struct val *res);
isn *isn_cmp(enum op_cmp, struct val *lhs, struct val *rhs, struct val *res);

isn *isn_zext(struct val *from, struct val *to);
isn *isn_sext(struct val *from, struct val *to);
isn *isn_trunc(struct val *from, struct val *to);

isn *isn_ptr2int(struct val *from, struct val *to);
isn *isn_int2ptr(struct val *from, struct val *to);
isn *isn_ptrcast(struct val *from, struct val *to);

isn *isn_br(struct val *cond, block *btrue, block *bfalse);
isn *isn_jmp(block *);

isn *isn_ret(struct val *);

isn *isn_call(struct val *into, struct val *fn, dynarray *args /*not consumed*/);

/* used for preserving register allocations until their (implicit) use,
 * e.g. call, end of prologue argument spill */
isn *isn_implicit_use(void);
void isn_implicit_use_add(isn *, struct val *);

bool isn_is_noop(struct isn *, struct val **src, struct val **dest);

bool isn_call_getfnval_args(isn *, struct val **, dynarray **);
struct val *isn_is_ret(isn *);

isn *isn_first(isn *);
isn *isn_next(isn *);

void isn_insert_before(isn *, isn *);
void isn_insert_after(isn *, isn *);

#endif
