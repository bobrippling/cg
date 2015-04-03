#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <sys/utsname.h>

#include "die.h"
#include "str.h"

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

static struct
{
	const char *name;
	void (*emit)(block *);
} backends[] = {
	{ "ir", block_dump },
	{ "x86_64", x86_out },
	{ "amd64",  x86_out },
	{ 0 }
};

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
		val *added = val_add(btrue,
				val_zext(btrue, cmp, INT_SIZE),
				val_new_i(1, INT_SIZE));

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
	int i;

	fprintf(stderr,
			"Usage: %s [options] [file | --eg[-jmp]]\n"
			"Options:\n"
			"  -O: optimise\n"
			"  --dump-tokens: token dump\n"
			"  --emit=<backend>: emit via backend:\n"
			, arg0);

	for(i = 0; backends[i].name; i++)
		fprintf(stderr, "    %s\n", backends[i].name);

	exit(1);
}

static void (*find_machine(const char *machine))(block *)
{
	int i;

	for(i = 0; backends[i].name; i++)
		if(!strcmp(machine, backends[i].name))
			return backends[i].emit;

	return NULL;
}

static void (*default_backend(void))(block *)
{
	struct utsname unam;
	void (*fn)(block *);

	if(uname(&unam))
		die("uname:");

	fn = find_machine(unam.machine);
	if(!fn)
		die("unknown machine '%s'", unam.machine);

	return fn;
}

int main(int argc, char *argv[])
{
	bool opt = false;
	bool dump_tok = false;
	void (*emit_fn)(block *) = NULL;
	bool skip_read = false;
	const char *fname = NULL;
	block *entry = block_new_entry();
	int i;
	int parse_err = 0;

	for(i = 1; i < argc; i++){
		if(!strcmp(argv[i], "-O")){
			opt = true;
		}else if(!strcmp(argv[i], "--dump-tokens")){
			dump_tok = true;

		}else if(str_beginswith(argv[i], "--emit=")){
			const char *backend = argv[i] + 7;

			if(emit_fn){
				fprintf(stderr, "already given a --emit option\n");
				usage(*argv);
			}

			emit_fn = find_machine(backend);
			if(!emit_fn)
				die("emit: unknown machine '%s'", backend);

		}else if(!strcmp(argv[i], "--help")){
			usage(*argv);

		}else if(!fname){
			fname = argv[i];

		}else{
			usage(*argv);
		}
	}

	if(fname){
		if(!strcmp(fname, "--eg")){
			eg1(entry);
			skip_read = true;
		}else if(!strcmp(fname, "--eg-jmp")){
			egjmp(entry);
			skip_read = true;
		}else if(!strcmp(fname, "-")){
			fname = NULL;
		}
	}

	if(!skip_read){
		parse_err = read_and_parse(fname, entry, dump_tok);
		if(dump_tok)
			return 0;
	}

	if(opt){
		opt_cprop(entry);
		opt_storeprop(entry);
		opt_dse(entry);
	}

	if(parse_err)
		return 1;

	if(!emit_fn)
		emit_fn = default_backend();
	emit_fn(entry);

	return 0;
}
