#ifndef MACHINE_ISN_H
#define MACHINE_ISN_H

struct machine_isn
{
	const char *mnemonic;
	int operand_count;

	struct machine_operand {
		enum {
			MACHINE_REG,
			MACHINE_MEM,
			MACHINE_INT
		} type;

		union {
			regt reg;
			struct {
				enum {
					MACHINE_MEM_LBL,
					MACHINE_MEM_REG
				} type;

				union {
					const char *lbl;
					struct {
						regt reg;
						int offset;
					} memreg;
				} u;
			} mem;
		} u;
	} inputs[2];
};

#endif
