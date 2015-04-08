#ifndef OP_H
#define OP_H

#define OPS   \
	X(add)      \
	X(sub)      \
	X(mul)      \
	X(div)      \
	X(mod)      \
	X(and)      \
	X(or)       \
	X(xor)      \
	X(and_sc)   \
	X(or_sc)    \
	X(shiftl)   \
	X(shiftr)   \
	X(shiftra)

#define CMPS \
	X(eq)      \
	X(ne)      \
	X(gt)      \
	X(ge)      \
	X(lt)      \
	X(le)

enum op
{
#define X(op) op_ ## op,
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

const char *op_to_str(enum op);
const char *op_cmp_to_str(enum op_cmp);

#endif
