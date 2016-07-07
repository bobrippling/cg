#ifndef VAL_STRUCT_H
#define VAL_STRUCT_H

#include <stdbool.h>

#include "reg.h"

struct name_loc
{
	enum
	{
		NAME_IN_REG,
		NAME_SPILT
	} where;
	union
	{
		regt reg;
		int off;
	} u;
};
#define name_loc_init_reg(nl) ((nl)->where = NAME_IN_REG, (nl)->u.reg = -1)
unsigned name_loc_hash(struct name_loc const *);

struct val
{
	struct type *ty;

	union
	{
		int i;
		struct global *global;
		struct
		{
			struct name_loc loc;
			char *name;
		} local, alloca;
		struct
		{
			/* location needs to be asked from the function */
			struct function *func;
			char *name;
			unsigned idx;
		} argument;
		struct name_loc temp_loc;
		struct name_loc abi;
	} u;

	void *pass_data;

	unsigned retains;
	bool live_across_blocks;

	enum val_kind
	{
		LITERAL,  /* i4 5, { i4, [i1 x 2] }* 54 */
		GLOBAL,   /* $x from global */
		ARGUMENT, /* $x from arg */
		FROM_ISN, /* $y = load i4* 1 */
		ALLOCA,   /* $z = alloca i4 */
		BACKEND_TEMP, /* mov $3, %eax ; ret */
		ABI_TEMP /* an actual register or stack slot */
	} kind;
};

typedef struct sym sym;

#endif
