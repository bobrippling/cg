#ifndef TARGET_H
#define TARGET_H

#include <stdbool.h>

#include "unit.h" /* global_emit_func */

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

		/* pic, pie, etc */
	} arch;

	struct target_sys
	{
		const char *lbl_priv_prefix;
		const char *section_rodata;
		const char *weak_directive_var;
		const char *weak_directive_func;
		bool align_is_pow2;
	} sys;

	struct target_abi
	{
		unsigned nregs;

		const unsigned *callee_save;
		unsigned callee_save_cnt;

		const unsigned *arg_regs_int;
		unsigned arg_regs_cnt_int;
		const unsigned *arg_regs_fp;
		unsigned arg_regs_cnt_fp;

		const unsigned *ret_regs_int;
		const unsigned ret_regs_cnt;
	} abi;

	global_emit_func *emit;
};

void target_parse(const char *triple, struct target *);
void target_default(struct target *);

#endif
