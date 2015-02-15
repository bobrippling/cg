#include "branch.h"

#include "block_struct.h"

void branch_cond(val *cond, block *current, block *btrue, block *bfalse)
{
	block_set_type(current, BLK_BRANCH);

	current->u.branch.cond = cond;
	current->u.branch.t = btrue;
	current->u.branch.f = bfalse;
}
