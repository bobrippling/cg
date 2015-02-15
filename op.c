#include <assert.h>

#include "op.h"

int op_exe(enum op op, int l, int r)
{
	switch(op){
		case op_add:
			return l + r;
	}
	assert(0);
}

const char *op_to_str(enum op op)
{
	switch(op){
		case op_add: return "add";
	}
	assert(0);
}
