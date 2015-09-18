#ifndef ISN_STRUCT_H
#define ISN_STRUCT_H

#include <stdbool.h>

#include "dynarray.h"
#include "op.h"

struct isn
{
	enum isn_type
	{
		ISN_LOAD,
		ISN_STORE,
		ISN_ALLOCA,
		ISN_OP,
		ISN_CMP,
		ISN_ELEM,
		ISN_PTRADD,
		ISN_COPY,
		ISN_EXT,
		ISN_PTR2INT,
		ISN_INT2PTR,
		ISN_BR,
		ISN_JMP,
		ISN_RET,
		ISN_CALL
	} type;

	union
	{
		struct
		{
			struct val *lval, *to;
		} load;
		struct
		{
			struct val *lval, *from;
		} store;

		struct
		{
			enum op op;
			struct val *lhs, *rhs, *res;
		} op;

		struct
		{
			enum op_cmp cmp;
			struct val *lhs, *rhs, *res;
		} cmp;

		struct
		{
			struct val *lval, *index, *res;
		} elem;

		struct
		{
			struct val *lhs, *rhs, *out;
		} ptradd;

		struct
		{
			struct val *out;
		} alloca;

		struct
		{
			struct val *from, *to;
		} copy, ptr2int;

		struct
		{
			struct val *from, *to;
			bool sign;
		} ext;

		struct
		{
			struct val *cond;
			block *t, *f;
		} branch;

		struct
		{
			block *target;
		} jmp;

		struct
		{
			struct val *into_or_null, *fn;
			dynarray args;
		} call;

		struct val *ret;
	} u;

	struct isn *next;

	bool skip;
};

const char *isn_type_to_str(enum isn_type);

#endif
