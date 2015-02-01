#include <stdio.h>
#include <assert.h>
#include <stdint.h>

#include "dynmap.h"
#include "isn_internal.h"
#include "isn_struct.h"
#include "val_struct.h"

static int alloca_offset(dynmap *alloca2stack, val *val)
{
	intptr_t off = dynmap_get(struct val *, intptr_t, alloca2stack, val);
	assert(off);
	return off;
}

static const char *x86_val_str(
		val *val, int bufchoice,
		dynmap *alloca2stack,
		bool dereference)
{
	static char buf1[256], buf2[256], buf3[256];
	static char (*bufs[]) = { buf1, buf2, buf3 };

	assert(0 <= bufchoice && bufchoice <= 2);
	char *buf = bufs[bufchoice];

	switch(val->type){
		case INT:
			assert(!dereference);
			snprintf(buf, sizeof buf1, "$%d", val->u.i);
			break;
		case INT_PTR:
			assert(dereference);
			snprintf(buf, sizeof buf1, "%d", val->u.i);
			break;
		case NAME:
			assert(!dereference);
			snprintf(buf, sizeof buf1, "$%s", val->u.addr.u.name);
			break;
		case NAME_LVAL:
			snprintf(buf, sizeof buf1, "%s%s%s",
					dereference ? "(" : "",
					val->u.addr.u.name,
					dereference ? ")" : "");
			break;
		case ALLOCA:
		{
			int off = alloca_offset(alloca2stack, val);
			/*assert(!dereference);*/
			snprintf(buf, sizeof buf1, "%d(%%rbp)", (int)off);
			break;
		}
	}

	return buf;
}

static void emit_elem(isn *i, dynmap *alloca2stack)
{
	int add_total;

	switch(i->u.elem.lval->type){
		case INT:
		case NAME:
			assert(0);

		case INT_PTR:
		{
			val *intptr = i->u.elem.lval;

			if(!val_op_maybe(op_add, i->u.elem.add, intptr, &add_total)){
				assert(0 && "couldn't add operands");
			}
			break;
		}

		case NAME_LVAL:
		{
			assert(0 && "TODO: add name_lval");
		}

		case ALLOCA:
		{
			assert(i->u.elem.add->type == INT);

			add_total = op_exe(op_add,
					alloca_offset(alloca2stack, i->u.elem.lval),
					i->u.elem.add->u.i);
			break;
		}
	}

	printf("\tlea %d(%%rbp), %s\n",
			add_total,
			x86_val_str(i->u.elem.res, 0, alloca2stack, 0));
}

void x86_out()
{
	isn *head = isn_head();
	dynmap *alloca2stack = dynmap_new(val *, /*ref*/NULL, val_hash);
	long alloca = 0;
	isn *i;

	/* gather allocas */
	for(i = head; i; i = i->next){
		if(i->skip)
			continue;

		switch(i->type){
			case ISN_ALLOCA:
			{
				alloca += i->u.alloca.sz;

				dynmap_set(val *, intptr_t,
						alloca2stack,
						i->u.alloca.out, -alloca);

				break;
			}

			default:
				break;
		}
	}

	printf("\tsub $%ld, %%rsp\n", alloca);

	for(i = head; i; i = i->next){
		if(i->skip)
			continue;

		switch(i->type){
			case ISN_ALLOCA:
				break;

			case ISN_STORE:
			{
				printf("\tmov %s, %s\n",
							x86_val_str(i->u.store.from, 1, alloca2stack, 0),
							x86_val_str(i->u.store.lval, 0, alloca2stack, 1));
				break;
			}

			case ISN_LOAD:
			{
				val *rval = i->u.load.lval;

				printf("\tmov %s, %s\n",
						x86_val_str(rval, 0, alloca2stack, 1),
						x86_val_str(i->u.load.to, 1, alloca2stack, 0));

				break;
			}

			case ISN_ELEM:
				emit_elem(i, alloca2stack);
				break;

			case ISN_OP:
			{
				printf("\t%s %s, %s ===> %s\n",
						op_to_cmd(i->u.op.op),
						x86_val_str(i->u.op.lhs, 0, alloca2stack, 0),
						x86_val_str(i->u.op.rhs, 1, alloca2stack, 0),
						x86_val_str(i->u.op.res, 2, alloca2stack, 0));
				break;
			}

			case ISN_COPY:
			{
				printf("\tmov %s, %s\n",
						x86_val_str(i->u.copy.from, 0, alloca2stack, 0),
						x86_val_str(i->u.copy.to, 1, alloca2stack, 0));
				break;
			}
		}
	}
}
