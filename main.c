#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>

#include <sys/utsname.h>

#include "die.h"
#include "str.h"

#include "backend.h"
#include "isn.h"
#include "unit.h"

#include "tokenise.h"
#include "parse.h"

#include "isn_internal.h" /* isn_dump() */

#include "opt_cprop.h"
#include "opt_storeprop.h"
#include "opt_dse.h"
#include "opt_loadmerge.h"
#include "x86.h"

static struct
{
	const char *name;
	void (*emit)(global *);
} backends[] = {
	{ "ir", global_dump },
	{ "x86_64", x86_out },
	{ "amd64",  x86_out },
	{ 0 }
};

enum
{
	INT_SIZE = 4
};

static void eg1(function *fn, block *const entry)
{
	val *a = val_new_i(3, INT_SIZE);
	val *b = val_new_i(5, INT_SIZE);
	val *store = val_new_ptr_from_int(0);

	(void)fn;

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
			val_element(entry, other_store, 1, INT_SIZE, NULL));

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

	val *alloca_p = val_element(entry, other_store, 1, 4, NULL);

	val *final = val_add(entry, val_load(entry, alloca_p, INT_SIZE), add_again);
	/* 9 + 25 = 34 */

	val_ret(entry, final);
}

static void egjmp(function *const fn, block *const entry)
{
	val *arg = val_new_i(5, INT_SIZE);

	val *cmp = val_equal(entry, arg, val_new_i(3, INT_SIZE));

	block *btrue = function_block_new(fn);
	block *bfalse = function_block_new(fn);

	isn_br(entry, cmp, btrue, bfalse);

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

static unit *read_and_parse(
		const char *fname, bool dump_tok, int *const err)
{
	FILE *f;
	tokeniser *tok;
	int ferr;
	unit *unit = NULL;

	if(fname){
		f = fopen(fname, "r");
		if(!f)
			die("open %s:", fname);
	}else{
		fname = "<stdin>";
		f = stdin;
	}

	tok = token_init(f, fname);

	if(dump_tok){
		for(;;){
			enum token ct = token_next(tok);
			if(ct == tok_eof)
				break;
			printf("token %s\n", token_to_str(ct));
		}
	}else{
		unit = parse_code(tok, err);
	}

	token_fin(tok, &ferr);
	if(ferr){
		errno = ferr;
		die("read %s:", fname);
		*err = 1;
	}

	return unit;
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

static void (*find_machine(const char *machine))(global *)
{
	int i;

	for(i = 0; backends[i].name; i++)
		if(!strcmp(machine, backends[i].name))
			return backends[i].emit;

	return NULL;
}

static void (*default_backend(void))(global *)
{
	struct utsname unam;
	void (*fn)(global *);

	if(uname(&unam))
		die("uname:");

	fn = find_machine(unam.machine);
	if(!fn)
		die("unknown machine '%s'", unam.machine);

	return fn;
}

static void run_opts(function *fn)
{
	function_onblocks(fn, opt_cprop);
	function_onblocks(fn, opt_storeprop);
	function_onblocks(fn, opt_dse);
	function_onblocks(fn, opt_loadmerge);
}

int main(int argc, char *argv[])
{
	bool opt = false;
	bool dump_tok = false;
	void (*emit_fn)(global *) = NULL;
	unit *unit = NULL;
	const char *fname = NULL;
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
		int jmp = 0;

		if(!strcmp(fname, "--eg") || (jmp = 1, !strcmp(fname, "--eg-jmp"))){
			function *fn;

			unit = unit_new();
			fn = unit_function_new(unit, "main", INT_SIZE);

			(jmp ? egjmp : eg1)(fn, function_entry_block(fn, 1));

		}else if(!strcmp(fname, "-")){
			fname = NULL;
		}
	}

	if(!unit){
		unit = read_and_parse(fname, dump_tok, &parse_err);
		if(dump_tok){
			assert(!unit);
			return 0;
		}
		assert(unit);
	}

	if(opt){
		unit_on_functions(unit, run_opts);
	}

	if(parse_err)
		return 1;

	if(!emit_fn)
		emit_fn = default_backend();
	unit_on_globals(unit, emit_fn);

	unit_free(unit);

	return 0;
}
