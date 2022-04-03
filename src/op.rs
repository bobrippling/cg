#include <assert.h>

#include "op.h"

int op_exe(enum op op, int l, int r, int *const div0)
{
	*div0 = 0;

#define div0check() \
			if(r == 0){   \
				*div0 = 1;  \
				return 0;   \
			}

	switch(op){
		case op_add: return l + r;
		case op_sub: return l - r;
		case op_mul: return l * r;

		case op_sdiv:
			div0check();
			return l / r;
		case op_smod:
			div0check();
			return l % r;

		case op_udiv:
			div0check();
			return (unsigned)l / r;
		case op_umod:
			div0check();
			return (unsigned)l % r;

		case op_and: return l & r;
		case op_or: return l | r;
		case op_xor: return l ^ r;
		case op_shiftl: return l << r;
		case op_shiftr_arith: return l >> r;
		case op_shiftr_logic: return (unsigned)l >> (unsigned)r;
	}
	assert(0);
}

int op_cmp_exe(enum op_cmp cmp, int l, int r)
{
	switch(cmp){
		case cmp_eq: return l == r;
		case cmp_ne: return l != r;
		case cmp_gt: return l >  r;
		case cmp_ge: return l >= r;
		case cmp_lt: return l <  r;
		case cmp_le: return l <= r;
	}
	assert(0);
}

const char *op_to_str(enum op op)
{
	switch(op){
#define X(op, match) case op_ ## op: return #op;
		OPS
#undef X
	}
	assert(0);
}

const char *op_cmp_to_str(enum op_cmp cmp)
{
	switch(cmp){
#define X(cmp) case cmp_ ## cmp: return #cmp;
		CMPS
#undef X
	}
	assert(0);
}

bool op_operands_must_match(enum op op)
{
	switch(op){
#define X(op, match) case op_ ## op: return match;
		OPS
#undef X
	}
	assert(0);
}
