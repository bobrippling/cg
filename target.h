#ifndef TARGET_H
#define TARGET_H

#include <stdbool.h>

#include "unit.h" /* global_emit_func */
#include "regset.h"

typedef void isel_func(struct isn *, const struct target *);

/* <arch><sub>-<vendor>-<sys>-<abi>
 * arch affects target_arch
 * vendor affects nothing
 * sys affects target_sys
 * abi affects nothing
 */
struct target
{
	struct target_arch
	{
		struct
		{
			unsigned size, align;
		} ptr;

		const struct target_arch_isn
		{
			const struct backend_isn *backend_isn; /* indexed by isn_type */
			isel_func *custom_isel;
		} *instructions;

		/* pic, pie, etc */
	} arch;

	struct target_sys
	{
		const char *lbl_priv_prefix;
		const char *section_rodata;
		const char *weak_directive_var;
		const char *weak_directive_func;
		bool align_is_pow2;
		bool leading_underscore;
	} sys;

	struct target_abi
	{
		struct regset scratch_regs;
		struct regset callee_saves;
		struct regset arg_regs;
		struct regset ret_regs;
	} abi;

	on_global_func *emit;
};

void target_parse(const char *triple, struct target *);
void target_default(struct target *);

#endif
