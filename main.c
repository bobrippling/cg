#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <errno.h>

#include "die.h"
#include "tristate.h"
#include "str.h"
#include "dynarray.h"

#include "backend.h"
#include "isn.h"
#include "unit.h"
#include "target.h"

#include "tokenise.h"
#include "parse.h"

#include "pass_abi.h"
#include "pass_isel.h"
#include "pass_spill.h"
#include "pass_regalloc.h"

#include "opt_cprop.h"
#include "opt_storeprop.h"
#include "opt_dse.h"
#include "opt_loadmerge.h"

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
	void (*fn)(function *, unit *, const struct target *);
} passes[] = {
	{ "_abi", pass_abi },
	{ "_isel", pass_isel },
	{ "_spill", pass_spill },
	{ "_regalloc", pass_regalloc },
#define X(n) { #n, opt_ ## n },
	OPTS
#undef X
};

static const char *argv0;

struct passes_and_target
{
	dynarray *passes;
	struct target *target;
	bool show_intermediates;
};

struct parsed_options
{
	tristate pic;
};

static unit *read_and_parse(
		const char *fname, bool dump_tok,
		const struct target *target, int *const err)
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
		unit = parse_code(tok, err, target);
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
	fprintf(stderr,
			"Usage: %s [options] [file | --eg[-jmp]]\n"
			"Options:\n"
			"  -O: optimise\n"
			"  --dump-tokens: token dump\n"
			"  --emit=<backend>: emit via backend:\n"
			"  -O<optimisation>: enable optimisation\n"
			, arg0);

#if 0
	for(i = 0; i < countof(passes); i++)
		fprintf(stderr, "    %s\n", passes[i].spel);
#endif

	exit(1);
}

static void run_passes(function *fn, unit *unit, void *vctx)
{
	const struct passes_and_target *pat = vctx;
	dynarray *passes_to_run = pat->passes;
	size_t i;

	dynarray_iter(passes_to_run, i){
		size_t j;
		const char *opt = dynarray_ent(passes_to_run, i);

		for(j = 0; j < countof(passes); j++)
			if(!strcmp(passes[j].spel, opt))
				break;

		if(j == countof(passes)){
			fprintf(stderr, "pass: unknown option '%s'\n", opt);
			usage(argv0);
		}

		if(pat->show_intermediates)
			printf("------- %s -------\n", passes[j].spel);

		passes[j].fn(fn, unit, pat->target);

		if(pat->show_intermediates)
			function_dump(fn, stdout);
	}
}

static void add_opts(struct target *target, const struct parsed_options *opts)
{
	if(opts->pic != TRISTATE_UNSET)
		target->sys.pic.active = opts->pic == TRISTATE_TRUE;
}

int main(int argc, char *argv[])
{
	FILE *fout;
	dynarray passes = DYNARRAY_INIT;
	bool dump_tok = false;
	unit *unit = NULL;
	const char *fname = NULL;
	const char *output = NULL;
	int i;
	int parse_err = 0;
	struct target target = { 0 };
	const char *emit_arg = NULL;
	struct passes_and_target pat = { 0 };
	struct parsed_options opts = { TRISTATE_UNSET };

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

		}else if(!strncmp(argv[i], "-fpic=", 6)){
			const char *arg = &argv[i][6];
			if(!strcmp(arg, "default")){
				opts.pic = TRISTATE_UNSET;
			}else if(!strcmp(arg, "false")){
				opts.pic = TRISTATE_FALSE;
			}else if(!strcmp(arg, "true")){
				opts.pic = TRISTATE_TRUE;
			}else{
				fprintf(stderr, "-fpic=... takes \"true\", \"false\" or \"default\"\n");
				usage(*argv);
			}

		}else if(!strcmp(argv[i], "--dump-tokens")){
			dump_tok = true;

		}else if(str_beginswith(argv[i], "--emit=")){
			const char *backend = argv[i] + 7;

			if(emit_arg){
				fprintf(stderr, "already given a --emit option\n");
				usage(*argv);
			}

			emit_arg = backend;

		}else if(!strcmp(argv[i], "--help")){
			usage(*argv);

		}else if(!strcmp(argv[i], "--show-intermediates")){
			pat.show_intermediates = true;

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

	if(emit_arg)
		target_parse(emit_arg, &target);
	else
		target_default(&target);
	add_opts(&target, &opts);

	unit = read_and_parse(fname, dump_tok, &target, &parse_err);
	if(dump_tok){
		assert(!unit);
		return 0;
	}
	assert(unit);
	if(parse_err)
		return 1;

	/* ensure the final passes are: */
	dynarray_add(&passes, "_abi");
	dynarray_add(&passes, "_isel");
	dynarray_add(&passes, "_spill");
	dynarray_add(&passes, "_regalloc");

	pat.passes = &passes;
	pat.target = &target;

	unit_on_functions(unit, run_passes, &pat);

	if(output){
		fout = fopen(output, "w");
		if(!fout)
			die("open %s:", output);
	}else{
		fout = stdout;
	}

	unit_on_globals(unit, target.emit, fout);

	unit_free(unit);

	dynarray_reset(&passes);

	fclose(fout);

	return 0;
}
