#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "die.h"

#include "backend.h"
#include "isn.h"
#include "block.h"
#include "branch.h"

#include "tokenise.h"
#include "parse.h"

#include "isn_internal.h" /* isn_dump() */
#include "block_internal.h" /* block_first_isn() */

#include "opt_cprop.h"
#include "opt_storeprop.h"
#include "opt_dse.h"
#include "x86.h"

enum
{
	INT_SIZE = 4
};

static void eg1(block *const entry)
{
	val *a = val_new_i(3, INT_SIZE);
	val *b = val_new_i(5, INT_SIZE);
	val *store = val_new_ptr_from_int(0);

	/* store = 3 */
	val_store(entry, a, store);

	/* loaded = 3 */
	val *loaded = val_load(entry, store, INT_SIZE);

	val *other_store = val_make_alloca(entry, 2, INT_SIZE);

	val_store(entry,
			val_new_i(7, INT_SIZE),
			other_store);

	val_store(entry,
			val_new_i(9, INT_SIZE),
			val_element(entry, other_store, 1, INT_SIZE));

	/* other_store = { 7, 9 } */

	val *added = val_add(entry,
			b,
			val_add(
				entry,
				val_load(entry, other_store, INT_SIZE),
				loaded));

	/* added = 5 + (7 + 3) = 15 */

	val *add_again =
		val_add(entry,
				val_add(entry,
					val_load(entry, store, INT_SIZE),
					val_load(entry, other_store, INT_SIZE)),
				added);

	/* add_again = (3 + 7) + 15 = 25 */

	val *alloca_p = val_element(entry, other_store, 1, 4);

	val *final = val_add(entry, val_load(entry, alloca_p, INT_SIZE), add_again);
	/* 9 + 25 = 34 */

	val_ret(entry, final);
}

static void egjmp(block *const entry)
{
	val *arg = val_new_i(5, INT_SIZE);

	val *cmp = val_equal(entry, arg, val_new_i(3, INT_SIZE));

	block *btrue = block_new(), *bfalse = block_new();

	branch_cond(cmp, entry, btrue, bfalse);

	val *escaped_bad;

	{
		val *added = val_add(btrue, cmp, val_new_i(1, INT_SIZE));
		val_ret(btrue, added);

		escaped_bad = added;
	}

	{
		val_ret(bfalse, escaped_bad); /*val_new_i(0));*/
	}
}

static int read_and_parse(const char *fname, block *entry, bool dump_tok)
{
	int err = 0;
	FILE *f;
	tokeniser *tok;
	int ferr;

	if(fname){
		f = fopen(fname, "r");
		if(!f)
			die("open %s:", fname);
	}else{
		fname = "<stdin>";
		f = stdin;
	}

	tok = token_init(f);

	if(dump_tok){
		for(;;){
			enum token ct = token_next(tok);
			if(ct == tok_eof)
				break;
			printf("token %s\n", token_to_str(ct));
		}
	}else{
		parse_code(tok, entry, &err);
	}

	token_fin(tok, &ferr);
	if(ferr){
		errno = ferr;
		die("read %s:", fname);
		err = 1;
	}

	return err;
}

static void usage(const char *arg0)
{
	fprintf(stderr, "Usage: %s [-O] [file | --eg[-jmp]]\n", arg0);
	exit(1);
}

int main(int argc, char *argv[])
{
	bool opt = false;
	bool dump_tok = false;
	const char *fname = NULL;
	block *entry = block_new_entry();
	int i;
	int parse_err = 0;

	for(i = 1; i < argc; i++){
		if(!strcmp(argv[i], "-O")){
			opt = true;
		}else if(!strcmp(argv[i], "--dump-tokens")){
			dump_tok = true;
		}else if(!fname){
			fname = argv[i];
		}else{
			usage(*argv);
		}
	}

	if(fname && !strcmp(fname, "--eg")){
		eg1(entry);
	}else if(fname && !strcmp(fname, "--eg-jmp")){
		egjmp(entry);
	}else{
		parse_err = read_and_parse(fname, entry, dump_tok);
		if(dump_tok)
			return 0;
	}

	if(opt){
		opt_cprop(entry);
		opt_storeprop(entry);
		opt_dse(entry);
	}

	block_dump(entry);

	if(parse_err)
		return 1;

	printf("x86:\n");

	x86_out(entry);

	return 0;
}
