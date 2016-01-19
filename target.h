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

		/* weak decl, etc */
	} sys;

	global_emit_func *emit;
};

void target_parse(const char *triple, struct target *);
void target_default(struct target *);

#endif
