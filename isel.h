#ifndef ISEL_H
#define ISEL_H

enum selected_operand_type {
	OP_IMMEDIATE,
	OP_REG,
	OP_OFFSET
};

struct selected_isn
{
	const struct backend_isn *backend_isn;

	struct {
		enum selected_operand_type type;
		union {
			int immediate;
			regt reg;

			/* ??? */
			struct {
				int offset_is_reg;
				union {
					regt reg;
					int immediate;
				} u;
			} offset;
		} u;
	} ops[2];
};

#endif
