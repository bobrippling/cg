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

static void simple_rval_resolve(val **const vaddr, dynmap *stores2rvals)
{
	val *found = resolve_val(*vaddr, stores2rvals);

	if(found && found != *vaddr)
		*vaddr = val_retain(found);
}

void opt_cprop(block *entry)
{
	dynmap *stores2rvals = dynmap_new(val *, /*ref*/NULL, val_hash);
	isn *i;

	for(i = block_first_isn(entry); i; i = i->next){
		switch(i->type){
			case ISN_STORE:
			{
				val *resolved_rval = resolve_val(i->u.store.from, stores2rvals);

				if(!resolved_rval)
					resolved_rval = i->u.store.from;

				(void)dynmap_set(val *, val *,
						stores2rvals,
						i->u.store.lval, resolved_rval);

				/* let the store remain */
				break;
			}

			case ISN_BR:
			{
				simple_rval_resolve(&i->u.branch.cond, stores2rvals);
				break;
			}

			case ISN_JMP:
			{
				break;
			}

			case ISN_RET:
			{
				simple_rval_resolve(&i->u.ret, stores2rvals);
				break;
			}

			case ISN_LOAD:
			{
				val *rval = resolve_val(i->u.load.lval, stores2rvals);

				if(rval){
					(void)dynmap_set(val *, val *,
							stores2rvals,
							i->u.load.to, rval);

					i->type = ISN_COPY;
					i->u.copy.from = val_retain(rval);
					i->u.copy.to = i->u.load.to;
				}
				break;
			}

			case ISN_EXT_TRUNC:
			{
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

			case ISN_CALL:
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
					(void)dynmap_set(val *, val *,
							stores2rvals,
							i->u.op.res, synth_add);

					i->type = ISN_COPY;
					i->u.copy.from = val_retain(synth_add);
					i->u.copy.to = i->u.op.res;
				}
				break;
			}

			case ISN_CMP:
			{
				val *solved_lhs = resolve_val(i->u.cmp.lhs, stores2rvals);
				val *solved_rhs = resolve_val(i->u.cmp.rhs, stores2rvals);
				int cmp_ret;

				if(!solved_lhs)
					solved_lhs = i->u.op.lhs;
				if(!solved_rhs)
					solved_rhs = i->u.op.rhs;

				if(val_cmp_maybe(i->u.cmp.cmp, solved_lhs, solved_rhs, &cmp_ret)){
					val *synth_cmp = val_new_i(cmp_ret, 1);

					(void)dynmap_set(val *, val *,
							stores2rvals,
							i->u.op.res, synth_cmp);

					i->type = ISN_COPY;
					i->u.copy.from = val_retain(synth_cmp);
					i->u.copy.to = i->u.op.res;
				}
				break;
			}
		}
	}

	dynmap_free(stores2rvals);
}
