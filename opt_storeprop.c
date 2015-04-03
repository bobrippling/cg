#include <stdio.h>

#include "dynmap.h"

#include "opt_storeprop.h"

#include "val_internal.h"
#include "val_struct.h"
#include "isn_internal.h"
#include "isn_struct.h"

#include "block_internal.h"

void opt_storeprop(block *const entry)
{
	dynmap *lval_entries = dynmap_new(val *, /*ref*/NULL, val_hash);
	isn *i;

	for(i = block_first_isn(entry); i; i = i->next){
		switch(i->type){
			case ISN_ELEM:
			{
				if(i->u.elem.add->type == INT){
					/* XXX: insert into right spot??? */
					val *elem = val_element_noop(
							i->u.elem.lval, i->u.elem.add->u.i.i, 1);

					if(elem){
						(void)dynmap_set(val *, val *,
								lval_entries,
								i->u.elem.res,
								elem);
					}
				}
				break;
			}

			case ISN_STORE:
			case ISN_LOAD:
			case ISN_ALLOCA:
			case ISN_COPY:
			case ISN_OP:
			case ISN_CMP:
			case ISN_RET:
				break;
		}
	}

	dynmap_free(lval_entries);
}
