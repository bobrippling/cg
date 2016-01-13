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

		default:
			assert(0 && "todo: init_dump type");
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
