#include <stdio.h>

#include "dynmap.h"

#include "opt_storeprop.h"

#include "val_internal.h"
#include "val_struct.h"
#include "isn_internal.h"
#include "isn_struct.h"

void opt_storeprop()
{
	dynmap *lval_entries = dynmap_new(val *, /*ref*/NULL, val_hash);
	isn *i;

	for(i = isn_head(); i; i = i->next){
		switch(i->type){
			case ISN_STORE:
			{
				if(i->u.store.lval->type == NAME_LVAL){
					/* mov $3, (%rax) - see where %rax points */
					val *actual = dynmap_get(
							val *, val *,
							lval_entries,
							i->u.store.lval);

					if(actual){
						val *from = i->u.store.from;
						val *dest = actual;

						i->type = ISN_STORE;
						i->u.store.lval = dest;
						i->u.store.from = from;
						break;
					}
				}
				break;
			}

			case ISN_ELEM:
			{
				if(i->u.elem.add->type == INT){
					val *elem = val_element(i->u.elem.lval, i->u.elem.add->u.i, 1);

					dynmap_set(val *, val *,
							lval_entries,
							i->u.elem.res,
							elem);
				}
				break;
			}

			case ISN_LOAD:
			case ISN_ALLOCA:
			case ISN_COPY:
			case ISN_OP:
				break;
		}
	}

	dynmap_free(lval_entries);
}
