#include "branch.h"

#include "block_struct.h"
#include "branch_internal.h"

void branch_cond(val *cond, block *current, block *btrue, block *bfalse)
{
	block_set_type(current, BLK_BRANCH);

	current->u.branch.cond = val_retain(cond);
	current->u.branch.t = btrue;
	current->u.branch.f = bfalse;
}

void branch_free(block *b)
{
	val_release(b->u.branch.cond);
}
