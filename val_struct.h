#ifndef VAL_STRUCT_H
#define VAL_STRUCT_H

#include <stdbool.h>

#include "reg.h"

#include "location.h"

struct val
{
	struct type *ty;

	union
	{
		int i;
		struct global *global;
		struct
		{
			struct location loc;
			char *name;
		} local, alloca, argument;
		struct location temp_loc;
		struct location abi;
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

const char *val_kind_to_str(enum val_kind);

#endif
