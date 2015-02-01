#ifndef ISN_PRIVATE_H
#define ISN_PRIVATE_H

#include "val_internal.h"

typedef struct isn isn;

struct isn
{
	enum isn_type
	{
		ISN_LOAD,
		ISN_STORE,
		ISN_ALLOCA,
		ISN_OP,
		ISN_ELEM,
		ISN_COPY
	} type;

	union
	{
		struct
		{
			val *lval, *to;
		} load;
		struct
		{
			val *lval, *from;
		} store;

		struct
		{
			enum op op;
			val *lhs, *rhs, *res;
		} op;

		struct
		{
			val *lval, *add, *res;
		} elem;

		struct
		{
			unsigned sz;
			val *out;
		} alloca;

		struct
		{
			val *from, *to;
		} copy;
	} u;

	isn *next;

	bool skip;
};

isn *isn_head(void);

#endif
