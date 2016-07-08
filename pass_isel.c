#include "function.h"

#include "pass_isel.h"

void pass_isel(function *fn, struct unit *unit, const struct target *target)
{
	/*
	 * - expand struct copy isns???????????????????????
	 * - reserve specific instructions to use certain registers,
	 *   e.g. x86 idiv, shift, cmp
	 * - load fp constants from memory
	 * - check constant size - if too large, need movabs
	 * - recognise 1(%rdi, %rax, 4) ?
	 *
	 * (check ucc's x86_64 isel stuff)
	 */
}
