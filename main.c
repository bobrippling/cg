#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "backend.h"
#include "isn.h"
#include "block.h"
#include "branch.h"

#include "isn_internal.h" /* isn_dump() */
#include "block_internal.h" /* block_first_isn() */

#include "opt_cprop.h"
#include "opt_storeprop.h"
#include "opt_dse.h"
#include "x86.h"

static void eg1(block *const entry)
{
	val *a = val_new_i(3);
	val *b = val_new_i(5);
	val *store = val_new_ptr_from_int(0);

	/* store = 3 */
	val_store(entry, a, store);

	/* loaded = 3 */
	val *loaded = val_load(entry, store);

	val *other_store = val_alloca(entry, 2, 4);

	val_store(entry, val_new_i(7), other_store);
	val_store(entry, val_new_i(9), val_element(entry, other_store, 1, 4));

	/* other_store = { 7, 9 } */

	val *added = val_add(entry,
			b,
			val_add(
				entry,
				val_load(entry, other_store),
				loaded));

	/* added = 5 + (7 + 3) = 15 */

	val *add_again =
		val_add(entry,
				val_add(entry,
					val_load(entry, store),
					val_load(entry, other_store)),
				added);

	/* add_again = (3 + 7) + 15 = 25 */

	val *alloca_p = val_element(entry, other_store, 1, 4);

	val *final = val_add(entry, val_load(entry, alloca_p), add_again);
	/* 9 + 25 = 34 */

	val_ret(entry, final);
}

static void egjmp(block *const entry)
{
	val *arg = val_new_i(5);

	val *cmp = val_equal(entry, arg, val_new_i(3));

	block *btrue = block_new(), *bfalse = block_new();

	branch_cond(cmp, entry, btrue, bfalse);

	val *escaped_bad;

	{
		val *added = val_add(btrue, cmp, val_new_i(1));
		val_ret(btrue, added);

		escaped_bad = added;
	}

	{
		val_ret(bfalse, escaped_bad); /*val_new_i(0));*/
	}
}

static void usage(const char *arg0)
{
	fprintf(stderr, "Usage: %s [-O] [jmp]\n", arg0);
	exit(1);
}

int main(int argc, char *argv[])
{
	bool opt = false;
	int i;
	void (*eg)(block *) = eg1;
	block *entry = block_new_entry();

	for(i = 1; i < argc; i++){
		if(!strcmp(argv[i], "-O")){
			opt = true;
		}else if(!strcmp(argv[i], "jmp")){
			eg = egjmp;
		}else{
			usage(*argv);
		}
	}

	eg(entry);

	if(opt){
		opt_cprop(entry);
		opt_storeprop(entry);
		opt_dse(entry);
	}

	block_dump(entry);

	printf("x86:\n");

	x86_out(entry);

	return 0;
}
