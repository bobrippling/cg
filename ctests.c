#include <stdio.h>
#include <unistd.h>

#include "die.h"

#include "unit.h"
#include "target.h"
#include "tokenise.h"
#include "parse.h"

#include "pass_abi.h"
#include "pass_isel.h"
#include "pass_spill.h"
#include "pass_regalloc.h"

static unit *compile_string(const char *str, int *const err, const struct target *target)
{
	struct {
		int parse, tok;
	} errs;
	tokeniser *tok = token_init_str(str);
	unit *u = parse_code(tok, &errs.parse, target);
	token_fin(tok, &errs.tok);

	*err = errs.parse + errs.tok;

	return u;
}

static void run_passes(function *fn, unit *unit, void *vctx)
{
	const struct target *target = vctx;

	pass_abi(fn, unit, target);
	pass_isel(fn, unit, target);
	pass_spill(fn, unit, target);
	pass_regalloc(fn, unit, target);
}

static int execute_ir(const struct target *target, const char *str)
{
	int err;
	unit *u = compile_string(str, &err, target);
	if(err){
		fprintf(stderr, "err\n");
		return 1;
	}
	unit_on_functions(u, run_passes, (void *)target);

	FILE *f = fopen("/tmp/dog.s", "w");/*tmpfile();*/
	if(!f)
		die("tmpfile:");
	/* FIXME: closing stdout/f, memleak / problems writing to stdout after etc etc */
	if(dup2(fileno(f), 1) < 0)
		die("dup2:");
	unit_on_globals(u, target->emit);
	fclose(f);
	fclose(stdout);

	if(system("echo 'int entry(void) __asm(\"entry\"); int main(){return entry();}' | cc -o /tmp/dog /tmp/dog.s -xc -"))
		die("system:");

	fprintf(stderr, "running /tmp/dog\n");
	return WEXITSTATUS(system("/tmp/dog"));
}

int main(int argc, const char *argv[])
{
	if(argc != 1){
		fprintf(stderr, "Usage: %s\n", argv[0]);
		return 1;
	}

	struct target target;
	target_parse("darwin-x86_64", &target);

	int ec = execute_ir(
			&target,
			"$is_5 = i4(i4 $x){"
			"  $b = eq $x, i4 5"
			"  $be = zext i4, $b"
			"  ret $be"
			"}"
			"$entry = i4(){"
			"  $x = call $is_5(i4 5)"
			"  ret $x"
			"}"
			);

	fprintf(stderr, "result: %d\n", ec);

	return 0;
}
