#include <stdio.h>
#include <assert.h>

#include "init.h"

void init_dump(struct init *init)
{
	switch(init->type){
		case init_int:
			printf("{ %#llx }", init->u.i);
			break;

		case init_str:
			putchar('\"');
			dump_escaped_string(&init->u.str);
			putchar('\"');
			break;

		default:
			assert(0 && "todo: init_dump type");
	}
}
