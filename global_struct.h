#ifndef GLOBAL_STRUCT_H
#define GLOBAL_STRUCT_H

struct global
{
	int is_fn;
	union
	{
		function *fn;
		variable_global *var;
	} u;
};

#endif
