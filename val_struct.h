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
			char *name;
		} label;
		struct
		{
			struct location loc;
			char *name;
		} local, alloca;
	} u;

	void *pass_data;

	unsigned retains;
	bool live_across_blocks;

	enum val_kind
	{
		LITERAL,  /* i4 5, { i4, [i1 x 2] }* 54 */
		GLOBAL,   /* $x from global */
		LABEL,    /* &&here */
		UNDEF,    /* undef */
		ALLOCA,   /* $z = alloca i4 */
		LOCAL     /* $y = load i4* 1 */
	} kind;

	enum val_flags
	{
		ABI = 1 << 0,
		ARG = 1 << 1,
		SPILL = 1 << 2
	} flags;
};

typedef struct sym sym;

const char *val_kind_to_str(enum val_kind);

#endif
