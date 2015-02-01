#ifndef OP_H
#define OP_H

enum op
{
	op_add
};

int op_exe(enum op, int l, int r);

const char *op_to_cmd(enum op);

#endif
