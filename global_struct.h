#ifndef GLOBAL_STRUCT_H
#define GLOBAL_STRUCT_H

struct global
{
	enum global_kind
	{
		GLOBAL_FUNC,
		GLOBAL_VAR,
		GLOBAL_TYPE
	} kind;

	union
	{
		function *fn;
		variable_global *var;
		struct type *ty;
	} u;
};

#endif
