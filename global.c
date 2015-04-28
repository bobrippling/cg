#include <stddef.h>
#include <stdio.h>

#include "global.h"

void global_dump(global *glob)
{
	if(glob->is_fn)
		function_dump(glob->u.fn);
	else
		variable_dump(glob->u.var);
}
