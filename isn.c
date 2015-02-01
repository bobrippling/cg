#include <stdio.h>

#include "mem.h"

#include "backend.h"
#include "val_internal.h"
#include "isn.h"
#include "isn_private.h"

static isn *head, **tail = &head;

isn *isn_head(void)
{
	return head;
}

static isn *isn_new(enum isn_type t)
{
	isn *isn = xcalloc(1, sizeof *isn);

	*tail = isn;
	tail = &isn->next;

	isn->type = t;
	return isn;
}

void isn_load(val *to, val *lval)
{
	isn *isn = isn_new(ISN_LOAD);

	isn->u.load.lval = lval;
	isn->u.load.to = to;
}

void isn_store(val *from, val *lval)
{
	isn *isn = isn_new(ISN_STORE);

	isn->u.store.lval = lval;
	isn->u.store.from = from;
}

void isn_op(enum op op, val *lhs, val *rhs, val *res)
{
	isn *isn = isn_new(ISN_OP);
	isn->u.op.op = op;
	isn->u.op.lhs = lhs;
	isn->u.op.rhs = rhs;
	isn->u.op.res = res;
}

void isn_elem(val *lval, val *add, val *res)
{
	isn *isn = isn_new(ISN_ELEM);
	isn->u.elem.lval = lval;
	isn->u.elem.add = add;
	isn->u.elem.res = res;
}

void isn_alloca(unsigned sz, val *v)
{
	isn *isn = isn_new(ISN_ALLOCA);
	isn->u.alloca.sz = sz;
	isn->u.alloca.out = v;
}

void isn_dump()
{
	isn *i;

	for(i = head; i; i = i->next){
		if(i->skip)
			continue;

		switch(i->type){
			case ISN_STORE:
			{
				printf("\tstore %s, %s\n",
							val_str(i->u.load.lval),
							val_str(i->u.load.to));
				break;
			}

			case ISN_LOAD:
			{
				val *rval = i->u.load.lval;

				printf("\t%s = load %s\n",
						val_str(i->u.load.to),
						val_str(rval));

				break;
			}

			case ISN_ALLOCA:
			{
				printf("\t%s = alloca %u\n",
						val_str(i->u.alloca.out),
						i->u.alloca.sz);
				break;
			}

			case ISN_ELEM:
			{
				printf("\t%s = elem %s, %s\n",
							val_str(i->u.elem.res),
							val_str(i->u.elem.lval),
							val_str(i->u.elem.add));
				break;
			}

			case ISN_OP:
			{
				printf("\t%s = %s %s, %s\n",
						val_str(i->u.op.res),
						op_to_cmd(i->u.op.op),
						val_str(i->u.op.lhs),
						val_str(i->u.op.rhs));
				break;
			}

			case ISN_COPY:
			{
				printf("\t%s = %s\n",
						val_str(i->u.copy.to),
						val_str(i->u.copy.from));
				break;
			}
		}
	}
}
