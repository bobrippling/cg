#include "../location.h"

#include "stack.h"

#include "../function.h"

void stack_alloc(struct location *loc, struct function *fn, struct type *ty)
{
	loc->where = NAME_SPILT;
	loc->u.off = function_alloc_stack_space(fn, ty);
}
