#ifndef VAL_STRUCT_H
#define VAL_STRUCT_H

struct name_loc
{
	enum
	{
		NAME_IN_REG,
		NAME_SPILT
	} where;
	union
	{
		int reg;
		unsigned off;
	} u;
};

struct val
{
	enum val_type
	{
		INT,       /* sized */
		INT_PTR,
		NAME,      /* sized */
		ALLOCA,
		LBL
	} type;
	unsigned retains;

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
					int val_size;
					struct name_loc loc;
				} name;
				struct
				{
					unsigned bytesz;
					int idx;
				} alloca;
				char *lbl;
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
