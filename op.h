#ifndef OP_H
#define OP_H

#include <stdbool.h>

#define OPS          \
	X(add, 1)          \
	X(sub, 1)          \
	X(mul, 1)          \
	X(sdiv, 1)         \
	X(udiv, 1)         \
	X(smod, 1)         \
	X(umod, 1)         \
	X(and, 1)          \
	X(or, 1)           \
	X(xor, 1)          \
	X(shiftl, 0)       \
	X(shiftr_logic, 0) \
	X(shiftr_arith, 0)

#define CMPS \
	X(eq)      \
	X(ne)      \
	X(gt)      \
	X(ge)      \
	X(lt)      \
	X(le)

enum op
{
#define X(op, match) op_ ## op,
	OPS
#undef X
};

enum op_cmp
{
#define X(cmp) cmp_ ## cmp,
	CMPS
#undef X
};

int op_exe(enum op, int l, int r, int *div0);

int op_cmp_exe(enum op_cmp, int l, int r);

bool op_operands_must_match(enum op);

const char *op_to_str(enum op);
const char *op_cmp_to_str(enum op_cmp);

#endif
