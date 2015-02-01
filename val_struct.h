#ifndef VAL_STRUCT_H
#define VAL_STRUCT_H

struct val
{
	enum val_type
	{
		INT,
		INT_PTR,
		NAME,
		NAME_LVAL,
		ALLOCA
	} type;

	union
	{
		int i;
		struct
		{
			union
			{
				char *name;
				struct
				{
					unsigned bytesz;
					int idx;
				} alloca;
			} u;
			struct val_idxpair
			{
				val *val;
				unsigned idx;
				struct val_idxpair *next;
			} *idxpair;
			/* val* => val*
			 * where .first is a INT */
		} addr;
	} u;
};

#endif
