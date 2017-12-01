#ifndef ISN_STRUCT_H
#define ISN_STRUCT_H

#include <stdbool.h>

#include "dynarray.h"
#include "op.h"
#include "regset_marks.h"
#include "string.h"

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
		ISN_PTRSUB,
		ISN_COPY,
		ISN_EXT_TRUNC,
		ISN_PTR2INT,
		ISN_INT2PTR,
		ISN_PTRCAST,
		ISN_BR,
		ISN_JMP,
		ISN_JMP_COMPUTED,
		ISN_LABEL,
		ISN_RET,
		ISN_CALL,
		ISN_ASM,
		ISN_IMPLICIT_USE_START,
		ISN_IMPLICIT_USE_END
#define ISN_TYPE_COUNT (ISN_IMPLICIT_USE_END+1)
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
		} ptraddsub;

		struct
		{
			struct val *out;
		} alloca;

		struct
		{
			struct val *from, *to;
		} copy, ptr2int, ptrcast;

		struct
		{
			struct val *from, *to;
			bool sign;
		} ext;

		struct
		{
			struct val *cond;
			struct block *t, *f;
		} branch;

		struct
		{
			struct block *target;
		} jmp;

		struct
		{
			struct val *target;
		} jmpcomp;

		struct
		{
			struct val *val;
		} lbl;

		struct
		{
			struct val *into, *fn;
			dynarray args;
		} call;

		struct val *ret;

		struct string as;

		struct
		{
			struct isn *link;
		} implicit_use_start;

		struct
		{
			dynarray vals;
		} implicit_use_end;
	} u;

	struct isn *next, *prev;
	regset_marks regusemarks;
	dynarray clobbers;

	bool skip;
	bool flag; /* for spills */
};

const char *isn_type_to_str(enum isn_type);
struct isn *isn_new(enum isn_type t);

#define isn_is_implicituse(t) ((t) == ISN_IMPLICIT_USE_START || (t) == ISN_IMPLICIT_USE_END)

#define isn_implicit_use_vals(i) \
	(&((i)->type == ISN_IMPLICIT_USE_START ? (i)->u.implicit_use_start.link : (i))->u.implicit_use_end.vals)

#endif
