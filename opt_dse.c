#include <stdlib.h>
#include <assert.h>

#include "mem.h"

#include "opt_dse.h"

#include "isn_internal.h"
#include "isn_struct.h"
#include "val_struct.h"

struct last_access
{
	unsigned last_access_isn;
};

static void store_access_times(val *v, isn *isn, void *ctx)
{
	struct last_access *la;

	(void)isn;

	if(!(la = v->pass_data))
		v->pass_data = la = xmalloc(sizeof *la);

	la->last_access_isn = *(unsigned *)ctx;
}

static void free_access_times(val *v, isn *isn, void *ctx)
{
	(void)isn;
	(void)ctx;

	free(v->pass_data);
	v->pass_data = NULL;
}

static void discard_dead_stores(isn *head)
{
	unsigned isn_count = 0;

	for(; head; head = head->next, isn_count++){
		val *use_val;
		struct last_access *la;
		bool check_global = false; /* keep stores to globals? */

		switch(head->type){
			default:
				continue;

			case ISN_STORE:
				use_val = head->u.store.lval;
				check_global = true;
				break;

			case ISN_LOAD:
				use_val = head->u.load.to;
				break;

			case ISN_COPY:
				use_val = head->u.copy.to;
				check_global = true;
				break;

			case ISN_ELEM:
				use_val = head->u.elem.res;
				check_global = true;
				break;
		}

		if(check_global && use_val->type == LBL)
			continue;

		assert(use_val->pass_data);
		la = use_val->pass_data;
		if(la->last_access_isn == isn_count){
			/* this store is the last access to 'use_val'
			 * - can be discarded */
			head->skip = 1;
		}
	}
}

void opt_dse(block *const entry)
{
	unsigned isn_count = 0;
	isn *const head = block_first_isn(entry);
	isn *i;

	for(i = head; i; i = i->next, isn_count++)
		isn_on_live_vals(i, store_access_times, &isn_count);

	discard_dead_stores(head);

	for(i = head; i; i = i->next)
		isn_on_all_vals(i, free_access_times, NULL);
}
