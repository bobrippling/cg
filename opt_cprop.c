#include <stdio.h>

#include "dynmap.h"

#include "opt_cprop.h"

#include "val_internal.h"
#include "val_struct.h"
#include "isn_internal.h"
#include "isn_struct.h"


static val *resolve_val(val *initial, dynmap *stores2rvals)
{
	return dynmap_get(val *, val *, stores2rvals, initial);
}

void opt_cprop()
{
	dynmap *stores2rvals = dynmap_new(val *, /*ref*/NULL, val_hash);
	isn *i;

	for(i = isn_head(); i; i = i->next){
		switch(i->type){
			case ISN_STORE:
			{
				val *resolved_rval = resolve_val(i->u.store.from, stores2rvals);

				if(!resolved_rval)
					resolved_rval = i->u.store.from;

				dynmap_set(val *, val *,
						stores2rvals,
						i->u.store.lval, resolved_rval);

				/* let the store remain */
				break;
			}

			case ISN_RET:
			{
				val *ret = resolve_val(i->u.ret, stores2rvals);

				if(ret && ret != i->u.ret){
					i->u.ret = ret;
				}
				break;
			}

			case ISN_LOAD:
			{
				val *rval = resolve_val(i->u.load.lval, stores2rvals);

				if(rval){
					dynmap_set(val *, val *,
							stores2rvals,
							i->u.load.to, rval);

					i->type = ISN_COPY;
					i->u.copy.from = rval;
					i->u.copy.to = i->u.load.to;
				}
				break;
			}

			case ISN_ALLOCA:
			{
				break;
			}

			case ISN_ELEM:
			{
				break;
			}

			case ISN_COPY:
			{
				break;
			}

			case ISN_OP:
			{
				val *solved_lhs = resolve_val(i->u.op.lhs, stores2rvals);
				val *solved_rhs = resolve_val(i->u.op.rhs, stores2rvals);
				val *synth_add;

				if(!solved_lhs)
					solved_lhs = i->u.op.lhs;
				if(!solved_rhs)
					solved_rhs = i->u.op.rhs;

				if(val_op_maybe_val(i->u.op.op, solved_lhs, solved_rhs, &synth_add)){
					dynmap_set(val *, val *,
							stores2rvals,
							i->u.op.res, synth_add);

					i->type = ISN_COPY;
					i->u.copy.from = synth_add;
					i->u.copy.to = i->u.op.res;
				}
				break;
			}
		}
	}

	dynmap_free(stores2rvals);
}
