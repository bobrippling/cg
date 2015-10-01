#ifndef X86_INTERNAL_H
#define X86_INTERNAL_H

typedef struct x86_octx
{
	dynmap *alloca2stack;
	block *exitblk;
	function *func;
	struct unit *unit;
	FILE *fout;
	long alloca_bottom; /* max of ALLOCA instructions */
	unsigned long spill_alloca_max; /* max of spill space */
	unsigned max_align;
	bool scratch_reg_reserved;
} x86_octx;

#define SCRATCH_REG 2 /* ecx */

/* ===--- value generation ---=== */
void x86_make_stack_slot(struct val *stack_slot, unsigned off, struct type *ty);

attr_nonnull()
void x86_make_reg(struct val *reg, int regidx, struct type *ty);

void x86_make_eax(struct val *out, struct type *ty);


/* ===--- commenting ---=== */
void x86_comment(x86_octx *octx, const char *fmt, ...);


/* ===--- value moving ---=== */
void x86_mov_deref(
		struct val *from, struct val *to,
		x86_octx *,
		bool deref_from, bool deref_to);

void x86_mov(struct val *from, struct val *to, x86_octx *octx);

#endif
