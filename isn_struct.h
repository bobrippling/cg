#ifndef ISN_STRUCT_H
#define ISN_STRUCT_H

#include <stdbool.h>

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
		ISN_COPY,
		ISN_EXT,
		ISN_BR,
		ISN_JMP,
		ISN_RET,
		ISN_CALL
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
			enum op_cmp cmp;
			val *lhs, *rhs, *res;
		} cmp;

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
		} copy, ext;

		struct
		{
			val *cond;
			block *t, *f;
		} branch;

		struct
		{
			block *target;
		} jmp;

		struct
		{
			val *into, *fn;
		} call;

		val *ret;
	} u;

	struct isn *next;

	bool skip;
};

const char *isn_type_to_str(enum isn_type);

#endif
