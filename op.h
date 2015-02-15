#ifndef OP_H
#define OP_H

enum op
{
	op_add
};

enum op_cmp
{
	op_cmp_eq
};

int op_exe(enum op, int l, int r);

int op_cmp_exe(enum op, int l, int r);

const char *op_to_str(enum op);
const char *op_cmp_to_str(enum op_cmp);

#endif
