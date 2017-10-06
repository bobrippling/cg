#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "die.h"
#include "mem.h"
#include "io.h"

#include "unit.h"
#include "target.h"
#include "tokenise.h"
#include "parse.h"

#include "pass_abi.h"
#include "pass_isel.h"
#include "pass_spill.h"
#include "pass_regalloc.h"

struct compile_ctx
{
	struct error
	{
		int line;
		char *fmt;

		struct error *next;
	} *errors;

};

struct path_and_file
{
	char *path;
	FILE *f;
};

static unsigned failed, passed;

static void free_path_and_file(struct path_and_file *paf)
{
	if(paf->f && fclose(paf->f))
		die("close %s:", paf->path);

	unlink(paf->path);
	free(paf->path);
}

static void free_compile_ctx(struct compile_ctx *ctx)
{
	struct error *e, *next;

	for(e = ctx->errors; e; e = next){
		next = e->next;
		free(e->fmt);
		free(e);
	}
}

static void on_error_store(
		const char *file,
		int line,
		void *vctx,
		const char *fmt,
		va_list l)
{
	struct compile_ctx *ctx = vctx;
	struct error *e = xmalloc(sizeof(*e));

	(void)file;
	(void)l;

	e->line = line;
	e->fmt = xstrdup(fmt);

	e->next = ctx->errors;
	ctx->errors = e;
}

static void on_error_simple(
		const char *file,
		int line,
		void *vctx,
		const char *fmt,
		va_list l)
{
	int *ctx = vctx;
	*ctx = 1;
}

static unit *compile_string(const char *str, int *const err, const struct target *target)
{
	tokeniser *tok = token_init_str(str);
	int parse_err = 0;
	unit *u = parse_code_cb(tok, target, on_error_simple, &parse_err);
	token_fin(tok, err);
	*err |= parse_err;
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

static unit *compile_and_pass_string(const char *str, int *const err, const struct target *target)
{
	unit *u = compile_string(str, err, target);
	if(!*err)
		unit_on_functions(u, run_passes, (void *)target);
	return u;
}

static void test_ir_compiles(const char *str, const struct target *target)
{
	int err;
	unit *u = compile_and_pass_string(str, &err, target);

	unit_free(u);

	if(err){
		fprintf(stderr, "compilation failure\n");
		failed++;
	}else{
		passed++;
	}
}

static int assemble_ir(const char *str, const struct target *target)
{
	struct path_and_file as = { 0 };
	int ec = 0;
	char sysbuf[256];
	int err;
	unit *u = compile_and_pass_string(str, &err, target);

	if(err){
		ec = -1;
		goto out;
	}

	as.f = temp_file(&as.path);
	if(!as.f)
		die("open %s:", as.path);

	unit_on_globals(u, target->emit, as.f);
	if(fclose(as.f))
		die("close:");
	as.f = NULL;

	xsnprintf(sysbuf, sizeof(sysbuf), "as -o /dev/null %s", as.path);
	ec = system(sysbuf);

out:
	unit_free(u);
	free_path_and_file(&as);

	if(WIFEXITED(ec))
		return WEXITSTATUS(ec);
	return -1;
}

static void test_ir_assembles(const char *str, const struct target *target)
{
	int ec = assemble_ir(str, target);

	if(ec){
		fprintf(stderr, "as return failure\n");
		failed++;
	}else{
		passed++;
	}
}

static int execute_ir(
		const struct target *target, int *const err, const char *str,
		const char *c_harness)
{
	struct path_and_file as = { 0 }, exe = { 0 };
	int ec = 0;
	int build_err;
	char sysbuf[512];
	unit *u = compile_and_pass_string(str, err, target);

	if(*err)
		goto out_err;

	as.f = temp_file(&as.path);
	if(!as.f)
		die("open %s:", as.path);

	unit_on_globals(u, target->emit, as.f);
	if(fclose(as.f))
		die("close:");
	as.f = NULL;

	exe.f = temp_file(&exe.path);

	xsnprintf(
			sysbuf,
			sizeof(sysbuf),
			"echo '%s' "
			"| cc -w -o %s -x assembler %s -xc -",
			c_harness,
			exe.path, as.path);

	build_err = system(sysbuf);
	if(build_err)
		goto out_err;

	FILE *newf = freopen(exe.path, "r", exe.f);
	if(!newf)
		die("freopen() executable:");
	exe.f = newf;

	ec = system(exe.path);

out:
	unit_free(u);
	free_path_and_file(&as);
	free_path_and_file(&exe);

	if(WIFEXITED(ec))
		return WEXITSTATUS(ec);
	return -1;

out_err:
	*err = 1;
	goto out;
}

static void test_ir_ret(
		const char *str, int ret, const struct target *target,
		const char *maybe_c_harness)
{
	const char *c_harness = "int entry(void) __asm__(\"entry\"); int main(){return entry();}";
	int err;
	int ec = execute_ir(target, &err, str, maybe_c_harness ? maybe_c_harness : c_harness);
	if(err || ec != ret){
		fprintf(stderr, "ir return failure, expected %d, got ", ret);
		if(err)
			fprintf(stderr, "error");
		else
			fprintf(stderr, "%d", ec);
		fprintf(stderr, "\n");

		failed++;
	}else{
		passed++;
	}
}

static void test_ir_x86(
		const char *ir,
		const char *x86,
		const struct target *target)
{
	int err;
	unit *u = compile_and_pass_string(ir, &err, target);

	if(err){
		fprintf(stderr, ":(\n");
		goto out;
	}

	FILE *f = tmpfile();
	if(!f)
		die("tmpfile:");

	unit_on_globals(u, target->emit, f);

	if(fseek(f, 0, SEEK_SET) < 0)
		die("fseek:");

	size_t nlines;
	char **lines = read_lines(f, &nlines);
	if(errno)
		die("read:");

	bool found = false;
	size_t i;
	for(i = 0; i < nlines; i++){
		if(strstr(lines[i], x86)){
			found = true;
		}
		free(lines[i]);
	}
	free(lines);

out:
	if(err){
		fprintf(stderr, "error compiling\n");
		failed++;
	}else if(!found){
		fprintf(stderr, "couldn't find \"%s\"\n", x86);
		failed++;
	}else{
		passed++;
	}

	unit_free(u);
}

static void test_ir_error(
		const char *ir,
		const struct target *target,
		const char *err, int line,
		...)
{
	tokeniser *tok = token_init_str(ir);
	struct compile_ctx ctx = { 0 };

	unit *u = parse_code_cb(tok, target, on_error_store, &ctx);
	int tok_err;
	token_fin(tok, &tok_err);
	unit_free(u);

	int had_error = 0;

	if(tok_err){
		fprintf(stderr, "tokenisation error\n");
		had_error = 1;
		goto out;
	}

	va_list l;
	va_start(l, line);

	struct error *e;
	for(e = ctx.errors; e; e = e->next){
		if(e->line != line){
			fprintf(stderr, "error mismatch: expected error on line %d, got it on line %d\n", line, e->line);
			had_error = 1;
			goto out_va_end;
		}else if(strcmp(e->fmt, err)){
			fprintf(stderr, "error mismatch: expected \"%s\", got \"%s\"\n", err, e->fmt);
			had_error = 1;
			goto out_va_end;
		}

		err = va_arg(l, const char *);
		if(!err){
			if(e->next){
				fprintf(stderr, "error count mismatch: got more than expected\n");
				had_error = 1;
				goto out_va_end;
			}
			break;
		}
		line = va_arg(l, int);
	}

	if(err){
		fprintf(stderr, "error count mismatch: got fewer than expected\n");
		had_error = 1;
	}

out_va_end:
	va_end(l);

out:
	free_compile_ctx(&ctx);

	if(had_error)
		failed++;
	else
		passed++;
}

int main(int argc, const char *argv[])
{
	if(argc != 1){
		fprintf(stderr, "Usage: %s\n", argv[0]);
		return 1;
	}

	struct target target;
	target_default(&target);

	test_ir_ret(
			"$is_5 = i4(i4 $x){"
			"  $b = eq $x, i4 5"
			"  $be = zext i4, $b"
			"  ret $be"
			"}"
			"$entry = i4(){"
			"  $x = call $is_5(i4 5)"
			"  ret $x"
			"}",
			1,
			&target,
			NULL);

	test_ir_ret(
			"$div = i4(i4 $a, i4 $b)"
			"{"
			"  $d = udiv $a, $b"
			"  $e = udiv $b, i4 2"
			"  $s = add $d, $e"
			"  ret $s"
			"}"
			"$entry = i4(){"
			"  $x = call $div(i4 10, i4 2)"
			"  ret $x"
			"}",
			6,
			&target,
			NULL);

	test_ir_ret(
			"type $a = {i4,i4}"
			"$f = $a() {"
			"  $stack = alloca $a"
			"  $p1 = elem $stack, i8 0"
			"  $p2 = elem $stack, i8 1"
			"  store $p1, i4 1"
			"  store $p2, i4 2"
			"  $tmp = load $stack"
			"  ret $tmp"
			"}",
			1,
			&target,
			"typedef struct { int a, b; } A;"
			"extern A f(void) __asm__(\"f\");"
			"int main() { A a = f(); return a.a == 1 && a.b == 2; }");

	test_ir_ret(
			"$x = i4 global 3"
			"$f = i4()"
			"{"
			"  $a = load $x"
			"  $b = load $x"
			"  $c = load $x"
			"  $d = load $x"
			"  $e = load $x"
			"  $1 = add $a, $b"
			"  $2 = add $1, $c"
			"  $3 = add $2, $d"
			"  $4 = add $3, $e"
			"  ret $4"
			"}"
			"$entry = i4()"
			"{"
			"	store $x, i4 3"
			"	$i = call $f()"
			"	ret $i"
			"}",
			15,
			&target,
			NULL);

	test_ir_ret(
			"$entry = i4()"
			"{"
			"  $stack1 = alloca i4"
			"  $stack2 = alloca i4"
			"  store $stack1, i4 5"
			"  store $stack2, i4 5"
			"  $e = eq $stack1, $stack2"
			"  $ez = zext i4, $e"
			"  ret $ez"
			"}",
			0,
			&target,
			NULL);

	test_ir_x86(
			"$f = i4(i4 $a){"
			"  ret i4 3"
			"}",
			"mov $3, %eax",
			&target);

	test_ir_x86(
			"$x = {i4,i2} global aliasinit i2 3",
			".word 0x3",
			&target);

	test_ir_x86(
			"$f = i4(i4 $a, i4 $b){ ret $b }",
			"mov %esi, %eax",
			&target);

	test_ir_error(
			"$main = i4(){\n"
			"	$z = alloca i4()\n"
			"	ret i4 0\n"
			"}\n",
			&target,
			"alloca of function type", 1,
			(char *)NULL);

	test_ir_compiles(
			"$f = i4*(){"
			"	$p = alloca i4"
			"	$plus = ptradd $p, i8 3"
			"	ret $plus"
			"}",
			&target);

	test_ir_compiles(
			"type $alias = { i4, { i1* }* }"
			"$i1 = i1 global 7"
			"$si1 = {i1*} global { $i1 }"
			"$globvar = $alias global { 3, $si1 }"
			"$globvar2 = {$alias, [i4 x 3]} internal { { 4, $si1 }, { 1, 2, 3 } }"
			"$main = i4() {"
			"	ret i4 3"
			"}"
			"$func = i4(i1 $arg) {"
			"	$stackalloc = alloca $alias"
			"	$var = call $main()"
			"	ret $var"
			"}",
			&target);

	test_ir_assembles(
			"$f = i4(i4 $i)"
			"{"
			"	label $lbl"
			"	$1 = alloca void*"
			"	store $1, $lbl"
			"	jmp $lbl"
			"$lbl:"
			"	ret i4 1"
			"}",
			&target);

	/* TODO:
	 * interested in:
	 *   -[X] ./test a b c; test $? -eq ...
	 *   -[X] machine output
	 *   -[X] errors
	 *   -[ ] irdiff (pre-pass)
	 *   -[ ] ir output (optimisation folding, jump threading, etc)
	 */

	printf("passed: %d, failed: %d\n", passed, failed);

	return !!failed;
}
