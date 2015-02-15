#ifndef ISN_STRUCT_H
#define ISN_STRUCT_H

struct isn
{
	enum isn_type
	{
		ISN_LOAD,
		ISN_STORE,
		ISN_ALLOCA,
		ISN_OP,
		ISN_ELEM,
		ISN_COPY,
		ISN_RET
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

		val *ret;
	} u;

	isn *next;

	bool skip;
};

const char *isn_type_to_str(enum isn_type);

#endif
