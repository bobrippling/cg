#ifndef PASS_EXPAND_BUILTINS_H
#define PASS_EXPAND_BUILTINS_H

struct function;
struct unit;
struct target;

void pass_expand_builtins(
		struct function *fn,
		struct unit *unit,
		const struct target *target);

#endif
