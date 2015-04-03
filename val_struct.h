#ifndef VAL_STRUCT_H
#define VAL_STRUCT_H

struct val
{
	enum val_type
	{
		INT,       /* sized */
		INT_PTR,
		NAME,      /* sized */
		ALLOCA
	} type;

	union
	{
		struct
		{
			int i;
			int val_size;
		} i;
		struct
		{
			union
			{
				struct
				{
					char *spel;
					int reg;
					int val_size;
				} name;
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

	void *pass_data;
};

#endif
