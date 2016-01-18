#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <errno.h>

#include <sys/utsname.h>

#include "die.h"
#include "str.h"
#include "dynarray.h"

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

static const struct
{
	const char *name;
	global_emit_func *emit;
} backends[] = {
	{ "ir", global_dump },
	{ "x86_64", x86_out },
	{ "amd64",  x86_out },
	{ 0 }
};

#if 0
#define OPTS          \
	X(cprop)            \
	X(storeprop)        \
	X(dse)              \
	X(loadmerge)
#else
#define OPTS
#endif

static const struct
{
	const char *spel;
	void (*fn)(block *);
} optimisations[] = {
#define X(n) { #n, opt_ ## n },
	OPTS
#undef X
};

static const char *argv0;

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
	size_t i;

	fprintf(stderr,
			"Usage: %s [options] [file | --eg[-jmp]]\n"
			"Options:\n"
			"  -O: optimise\n"
			"  --dump-tokens: token dump\n"
			"  --emit=<backend>: emit via backend:\n"
			, arg0);

	for(i = 0; backends[i].name; i++)
		fprintf(stderr, "    %s\n", backends[i].name);

	fprintf(stderr, "  -O<optimisation>: enable optimisation\n");

#if 0
	for(i = 0; i < countof(optimisations); i++)
		fprintf(stderr, "    %s\n", optimisations[i].spel);
#endif

	exit(1);
}

static global_emit_func *find_machine(const char *machine)
{
	int i;

	for(i = 0; backends[i].name; i++)
		if(!strcmp(machine, backends[i].name))
			return backends[i].emit;

	return NULL;
}

static global_emit_func *default_backend(void)
{
	struct utsname unam;
	global_emit_func *fn;

	if(uname(&unam))
		die("uname:");

	fn = find_machine(unam.machine);
	if(!fn)
		die("unknown machine '%s'", unam.machine);

	return fn;
}

static void run_opts(function *fn, void *vctx)
{
	dynarray *passes = vctx;
	size_t i;

	dynarray_iter(passes, i){
		size_t j;
		bool found = false;
		const char *opt = dynarray_ent(passes, i);

#if 0
		for(j = 0; j < countof(optimisations); j++){
			if(!strcmp(optimisations[j].spel, opt)){
				found = true;
				break;
			}
		}
#else
		j = 0;
#endif

		if(!found){
			fprintf(stderr, "optimise: unknown option '%s'\n", opt);
			usage(argv0);
		}

		function_onblocks(fn, optimisations[j].fn);
	}
}

int main(int argc, char *argv[])
{
	dynarray passes = DYNARRAY_INIT;
	bool dump_tok = false;
	global_emit_func *emit_fn = NULL;
	unit *unit = NULL;
	const char *fname = NULL;
	const char *output = NULL;
	int i;
	int parse_err = 0;

	argv0 = argv[0];

	for(i = 1; i < argc; i++){
		if(!strncmp(argv[i], "-O", 2)){
			if(argv[i][2]){
				dynarray_add(&passes, argv[i] + 2);
			}else{
#if 0
#define X(n) dynarray_add(&passes, #n);
				OPTS
#undef X
#endif
			}

		}else if(!strncmp(argv[i], "-o", 2)){
			if(output){
				fprintf(stderr, "already given a '-o' option\n");
				usage(*argv);
			}

			if(argv[i][2]){
				output = argv[i] + 2;
			}else{
				i++;
				if(!argv[i]){
					fprintf(stderr, "'-o' requires an argument\n");
					usage(*argv);
				}
				output = argv[i];
			}

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

		}else if(!strcmp(argv[i], "--")){
			i++;

			/* only expect one filename after "--" */

			if(argv[i] && !fname){
				fname = argv[i];
				i++;
			}

			if(argv[i])
				usage(*argv);

			break;

		}else if(!fname){
			fname = argv[i];

		}else{
			usage(*argv);
		}
	}

	if(fname && !strcmp(fname, "-"))
		fname = NULL;

	if(!unit){
		unit = read_and_parse(fname, dump_tok, &parse_err);
		if(dump_tok){
			assert(!unit);
			return 0;
		}
		assert(unit);
	}

	unit_on_functions(unit, run_opts, &passes);

	if(parse_err)
		return 1;

	if(output && !freopen(output, "w", stdout)){
		fprintf(stderr, "%s: open %s: %s\n", *argv, output, strerror(errno));
		return 1;
	}

	if(!emit_fn)
		emit_fn = default_backend();
	unit_on_globals(unit, emit_fn);

	unit_free(unit);

	dynarray_reset(&passes);

	return 0;
}
