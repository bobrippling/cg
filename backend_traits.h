#ifndef BACKEND_TRAITS_H
#define BACKEND_TRAITS_H

struct backend_traits
{
	int nregs;
	int scratch_reg;
	unsigned ptrsz;

	const int *callee_save;
	unsigned callee_save_cnt;

	const int *arg_regs;
	unsigned arg_regs_cnt;
};

#endif
