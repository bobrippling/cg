#ifndef FUNCTION_INTERNAL_H
#define FUNCTION_INTERNAL_H

struct dynarray;

function *function_new(
		const char *lbl, struct type *fnty,
		struct dynarray *toplvl_args,
		unsigned *uniq_counter);

#endif
