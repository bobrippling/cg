#include <stdio.h>

#include "mem.h"

#include "backend.h"
#include "val_internal.h"
#include "isn.h"
#include "isn_internal.h"
#include "isn_struct.h"

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

const char *isn_type_to_str(enum isn_type t)
{
	switch(t){
		case ISN_STORE:  return "store";
		case ISN_LOAD:   return "load";
		case ISN_ALLOCA: return "alloca";
		case ISN_ELEM:   return "elem";
		case ISN_OP:     return "op";
		case ISN_COPY:   return "copy";
		case ISN_RET:    return "ret";
	}
	return NULL;
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

void isn_ret(val *r)
{
	isn *isn = isn_new(ISN_RET);
	isn->u.ret = r;
}

void isn_on_vals(isn *current, void fn(val *, isn *, void *), void *ctx)
{
	switch(current->type){
		case ISN_STORE:
			fn(current->u.store.lval, current, ctx);
			fn(current->u.store.from, current, ctx);
			break;

		case ISN_LOAD:
			fn(current->u.load.to, current, ctx);
			fn(current->u.load.lval, current, ctx);
			break;

		case ISN_ALLOCA:
			fn(current->u.alloca.out, current, ctx);
			break;

		case ISN_ELEM:
			fn(current->u.elem.res, current, ctx);
			fn(current->u.elem.lval, current, ctx);
			fn(current->u.elem.add, current, ctx);
			break;

		case ISN_OP:
			fn(current->u.op.res, current, ctx);
			fn(current->u.op.lhs, current, ctx);
			fn(current->u.op.rhs, current, ctx);
			break;

		case ISN_COPY:
			fn(current->u.copy.to, current, ctx);
			fn(current->u.copy.from, current, ctx);
			break;

		case ISN_RET:
			fn(current->u.ret, current, ctx);
			break;
	}
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
							val_str(i->u.store.lval),
							val_str(i->u.store.from));
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

			case ISN_RET:
			{
				printf("\tret %s\n", val_str(i->u.ret));
				break;
			}
		}
	}
}
