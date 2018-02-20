#include "../mem.h"

#include "spill.h"

#include "type.h"
#include "val.h"
#include "isn.h"
#include "lifetime_struct.h"
#include "isn_replace.h"
#include "stack.h"

void spill(val *v, isn *before_isn, uniq_type_list *utl, struct function *fn, block *blk)
{
	type *const ty = val_type(v);
	struct lifetime *spill_lt = xmalloc(sizeof *spill_lt);
	const char *name = val_frontend_name(v);
	val *spill;
	isn *alloca;

	if(name){
		spill = val_new_localf(
				type_get_ptr(utl, ty),
				true,
				"spill.for.%s",
				name);
	}else{
		spill = val_new_localf(
				type_get_ptr(utl, ty),
				true,
				"spill.%d",
				/*something unique:*/(int)v);
	}
	stack_alloc(val_location(spill), fn, ty);

	alloca = isn_alloca(spill);
	isn_insert_before(before_isn, alloca);

	isn_replace_uses_with_load_store_isn(before_isn, v, spill, blk);
}

