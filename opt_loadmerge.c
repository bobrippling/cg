#include "dynmap.h"

#include "opt_loadmerge.h"

#include "val.h"
#include "val_struct.h"
#include "val_internal.h"
#include "op.h"
#include "isn_struct.h"

static int cmp_load_addr(const val *a, const val *b)
{
	enum { eq, neq };

	if(a == b)
		return eq;

	if(a->type != INT_PTR)
		return neq;
	if(b->type != INT_PTR)
		return neq;

	if(a->u.i.i != b->u.i.i)
		return neq;
	if(a->u.i.val_size != b->u.i.val_size)
		return neq;

	return eq;
}

void opt_loadmerge(block *const entry)
{
	dynmap *loads2rvals = dynmap_new(val *, cmp_load_addr, val_hash);
	struct isn *i;

	for(i = block_first_isn(entry); i; i = i->next){
		switch(i->type){
			case ISN_STORE:
			{
				/* invalidate i->u.store.lval */
				dynmap_rm(
						val *, val *,
						loads2rvals,
						i->u.store.lval);
				break;
			}

			case ISN_ELEM:
			{
				/* if we alias a store, invalidate it */
				dynmap_rm(
						val *, val *,
						loads2rvals,
						i->u.elem.lval);
				break;
			}

			case ISN_LOAD:
			{
				/* see if we've already loaded this
				 * (without a intermediate store/elem) */
				val *rval = dynmap_get(
						val *, val *,
						loads2rvals,
						i->u.load.lval);

				if(rval){
					/* can replace this load with a copy */
					val *to = i->u.load.to; /* union aliasing */

					i->type = ISN_COPY;
					i->u.copy.from = val_retain(rval);
					i->u.copy.to = to;

				}else{
					/* remember this load */
					(void)dynmap_set(val *, val *,
							loads2rvals,
							i->u.load.lval,
							i->u.load.to);
				}
				break;
			}

			case ISN_ALLOCA:
			case ISN_COPY:
			case ISN_OP:
			case ISN_CMP:
			case ISN_RET:
			case ISN_BR:
			case ISN_JMP:
			case ISN_EXT:
			case ISN_CALL:
				break;
		}
	}

	dynmap_free(loads2rvals);
}
