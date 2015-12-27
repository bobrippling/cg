#ifndef VAL_STRUCT_H
#define VAL_STRUCT_H

#include <stdbool.h>

struct name_loc
{
	enum
	{
		NAME_IN_REG,
		NAME_SPILT
	} where;
	union
	{
		int reg;
		unsigned off;
	} u;
};

struct val
{
	struct type *ty;

	union
	{
		int i;
		struct variable *global;
		struct sym
		{
			struct variable *var;
			struct name_loc loc;
		} argument, local;
	} u;

	void *pass_data;

	unsigned retains;
	bool live_across_blocks;

	enum val_kind
	{
		LITERAL,  /* i32 5, { i32, [i8 x 2] }* 54 */
		GLOBAL,   /* $x from global */
		ARGUMENT, /* $x from arg */
		FROM_ISN  /* $y = load i32* 1 */
	} kind;
};

#endif
