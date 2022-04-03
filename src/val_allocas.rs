#include <stddef.h>
#include <assert.h>

#include "mem.h"

#include "val_allocas.h"
#include "val_internal.h"
#include "val_struct.h"

val *val_alloca_idx_get(val *lval, unsigned idx)
{
	struct val_idxpair *pair;

	lval = VAL_NEED(lval, ADDRESSABLE);

	for(pair = lval->u.addr.idxpair;
			pair;
			pair = pair->next)
	{
		if(pair->idx == idx)
			return pair->val;
	}

	return NULL;
}

void val_alloca_idx_set(val *lval, unsigned idx, val *elemptr)
{
	lval = VAL_NEED(lval, ADDRESSABLE);

	struct val_idxpair **next;
	for(next = &lval->u.addr.idxpair;
			*next;
			next = &(*next)->next)
	{
		assert((*next)->idx != idx);
	}

	struct val_idxpair *newpair = xcalloc(1, sizeof *newpair);
	*next = newpair;
	newpair->val = elemptr;
	newpair->idx = idx;
}
