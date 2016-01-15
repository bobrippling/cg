#include <stdio.h>
#include <assert.h>

#include "init.h"

static void init_dump_r(struct init *init)
{
	switch(init->type){
		case init_int:
			printf("%#llx", init->u.i);
			break;

		case init_str:
			putchar('\"');
			dump_escaped_string(&init->u.str);
			putchar('\"');
			break;

		case init_array:
		case init_struct:
		{
			size_t i;
			const char *comma = "";

			printf("{ ");

			dynarray_iter(&init->u.elem_inits, i){
				printf("%s", comma);
				init_dump_r(dynarray_ent(&init->u.elem_inits, i));
				comma = ", ";
			}
			printf(" }");

			break;
		}

		case init_ptr:
		{
			long off = init->u.ptr.label.offset;

			printf("$%s %s %ld%s",
					init->u.ptr.label.ident,
					off > 0 ? "add" : "sub",
					off > 0 ? off : -off,
					init->u.ptr.is_anyptr ? " anyptr" : "");
			break;
		}
	}
}

void init_dump(struct init_toplvl *init)
{
	printf("%s ", init->internal ? "internal" : "global");

	if(init->constant)
		printf("const ");
	if(init->weak)
		printf("weak ");

	init_dump_r(init->init);
}
