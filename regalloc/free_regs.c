#include <stdint.h>
#include <assert.h>

#include "../dynarray.h"
#include "../target.h"

#include "free_regs.h"

static void get_max_reg(const struct regset *regs, unsigned *const max)
{
	size_t i;
	for(i = 0; i < regs->count; i++)
		if(regs->regs[i] > *max)
			*max = regs->regs[i];
}

static unsigned free_regs_total(const struct target *target)
{
	unsigned max = 0;
	get_max_reg(&target->abi.scratch_regs, &max);
	get_max_reg(&target->abi.callee_saves, &max);
	get_max_reg(&target->abi.arg_regs, &max);
	get_max_reg(&target->abi.ret_regs, &max);
	return max;
}

void free_regs_create(dynarray *regs, const struct target *target)
{
	unsigned i, max = free_regs_total(target);

	for(i = 0; i <= max; i++)
		dynarray_add(regs, (void *)(intptr_t)false);

	for(i = 0; i < target->abi.scratch_regs.count; i++)
		dynarray_ent(regs, target->abi.scratch_regs.regs[i]) = (void *)(intptr_t)true;
}

void free_regs_delete(dynarray *regs)
{
	dynarray_reset(regs);
}

unsigned free_regs_available(dynarray *a)
{
	size_t i;
	unsigned n = 0;
	dynarray_iter(a, i)
		n += !!dynarray_ent(a, i);
	return n;
}

unsigned free_regs_any(dynarray *free_regs)
{
	size_t i;
	for(i = 0; i < dynarray_count(free_regs); i++){
		if(dynarray_ent(free_regs, i)){
			return i;
		}
	}
	return -1;
}

void free_regs_merge(dynarray *dest, dynarray *src1, dynarray *src2)
{
	size_t i;
	assert(dynarray_count(src1) == dynarray_count(src2));

	dynarray_init(dest);

	dynarray_iter(src1, i){
		bool a = !!dynarray_ent(src1, i);
		bool b = !!dynarray_ent(src2, i);

		dynarray_add(dest, (void *)(intptr_t)(a && b));
	}
}
