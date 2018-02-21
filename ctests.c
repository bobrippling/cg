#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <dlfcn.h>

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

static void report(int lno, const char *errmsg)
{
	fprintf(stderr, "%s:%d: %s\n", __FILE__, lno, errmsg ? errmsg : "pass");

	if(errmsg){
		failed++;
	}else{
		passed++;
	}
}

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

	(void)file;
	(void)line;
	(void)fmt;
	(void)l;
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
	/*pass_spill(fn, unit, target);*/
	pass_regalloc(fn, unit, target);
}

static unit *compile_and_pass_string(const char *str, int *const err, const struct target *target)
{
	unit *u = compile_string(str, err, target);
	if(!*err)
		unit_on_functions(u, run_passes, (void *)target);
	return u;
}

static void test_ir_compiles(int lno, const char *str, const struct target *target)
{
	int err;
	unit *u = compile_and_pass_string(str, &err, target);

	unit_free(u);

	report(lno, err ? "compilation failure" : NULL);
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

static void test_ir_assembles(int lno, const char *str, const struct target *target)
{
	int ec = assemble_ir(str, target);

	report(lno, ec ? "as return failure" : NULL);
}

static int execute_ir_via_cc(
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

static int execute_ir_via_so(const struct target *target, int *const err, const char *str)
{
	struct path_and_file as = { 0 }, so = { 0 };
	int ec = 0;
	int so_err;
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

	so.f = temp_file(&so.path);

	xsnprintf(
			sysbuf,
			sizeof(sysbuf),
			"cc -shared -w -o %s -x assembler %s",
			so.path, as.path);

	so_err = system(sysbuf);
	if(so_err)
		goto out_err;

	void *dl = dlopen(so.path, RTLD_NOW | RTLD_LOCAL);
	if(!dl)
		die("dlopen(): %s", dlerror());

	int (*entry)(void) = dlsym(dl, "entry");
	if(!entry)
		die("dlsym(): %s", dlerror());

	ec = entry();
	dlclose(dl);

out:
	unit_free(u);
	free_path_and_file(&as);
	free_path_and_file(&so);

	return ec;

out_err:
	*err = 1;
	goto out;
}

static int execute_ir(
		const struct target *target, int *const err, const char *str,
		const char *c_harness)
{
	if(c_harness){
		return execute_ir_via_cc(target, err, str, c_harness);
	}else{
		return execute_ir_via_so(target, err, str);
	}
}

static void test_ir_ret(
		int lno,
		const char *str, int ret, const struct target *target,
		const char *maybe_c_harness)
{
	int err;
	int ec = execute_ir(target, &err, str, maybe_c_harness);
	char ebuf[64];
	const char *emsg = NULL;

	if(err){
		emsg = "ir compilation failure";
	}else if(ec != ret){
		xsnprintf(ebuf, sizeof(ebuf), "ir returned %#x, expected %#x", ec, ret);
		emsg = ebuf;
	}

	report(lno, emsg);
}

static void test_ir_emit(
		int lno,
		const char *ir,
		const char *x86,
		const struct target *target)
{
	int err;
	unit *u = compile_and_pass_string(ir, &err, target);
	if(err){
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

	const char *nl = strchr(x86, '\n');
	char *x86_tokens = (char *)x86;
	char *x86_dup = NULL;
	if(nl){
		x86_dup = xstrdup(x86);
		x86_tokens = strtok(x86_dup, "\n");
	}

	bool found = false;
	size_t i;
	for(i = 0; i < nlines; i++){
		if(strstr(lines[i], x86_tokens)){
			if(nl){
				/* next line */
				x86_tokens = strtok(NULL, "\n");
				if(!x86_tokens){
					found = true;
					nl = NULL;
					x86_tokens = "";
				}
			}else{
				/* a single line */
				found = true;
			}
		}
		free(lines[i]);
	}
	free(lines);
	free(x86_dup);

out:
	if(err){
		report(lno, "error compiling");
	}else if(!found){
		report(lno, "couldn't find emitted string");
	}else{
		report(lno, NULL);
	}

	unit_free(u);
}

static void test_ir_error(
		int lno,
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

	if(tok_err){
		fprintf(stderr, "tokenisation error\n");
		goto out;
	}

	va_list l;
	va_start(l, line);

	struct error *e;
	for(e = ctx.errors; e; e = e->next){
		if(e->line != line){
			report(lno, "mismatching error lines");
			goto out_va_end;
		}else if(strcmp(e->fmt, err)){
			report(lno, "mismatching error strings");
			goto out_va_end;
		}

		err = va_arg(l, const char *);
		if(!err){
			if(e->next){
				report(lno, "more errors than expected");
				goto out_va_end;
			}
			break;
		}
		line = va_arg(l, int);
	}

	if(err){
		report(lno, "fewer errors than expected");
	}

out_va_end:
	va_end(l);

out:
	free_compile_ctx(&ctx);
}

#define TEST(fn, ...) test_##fn(__LINE__, __VA_ARGS__)

int main(int argc, const char *argv[])
{
	if(argc != 1){
		fprintf(stderr, "Usage: %s\n", argv[0]);
		return 1;
	}

	struct target target, target_ir, target_nomangle;
	target_default(&target);
	target_parse("ir-linux", &target_ir);
	target_parse("x86_64-linux", &target_nomangle);

	target.sys.pic.active = true;

	TEST(ir_ret,
			"$is_5 = i4(i4 $x) internal {"
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

	TEST(ir_ret,
			"$div = i4(i4 $a, i4 $b) internal"
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

	TEST(ir_ret,
			"type $a = {i4,i4}"
			"$f = $a() {"
			"  $stack = alloca $a"
			"  $p1 = elem $stack, i8 0"
			"  $p2 = elem $stack, i8 1"
			"  store $p1, i4 1"
			"  store $p2, i4 2"
			"  ret $stack"
			"}",
			1,
			&target,
			"typedef struct { int a, b; } A;"
			"extern A f(void);"
			"int main() { A a = f(); return a.a == 1 && a.b == 2; }");

	TEST(ir_ret,
			"type $struct_mem = { i8, i8, i8 }"
			"$f = $struct_mem(i8 $1, i8 $2, i8 $3)"
			"{"
			" $a = alloca $struct_mem"
			""
			" $p0 = elem $a, i8 0"
			" store $p0, $1"
			" $p1 = elem $a, i8 1"
			" store $p1, $2"
			" $p2 = elem $a, i8 2"
			" store $p2, $3"
			""
			" ret $a"
			"}",
			1,
			&target,
			"typedef struct { long a, b, c; } A;"
			"extern A f(long a, long b, long c);"
			"int main() { A a = f(5, 9, 22); return a.a == 5 && a.b == 9 && a.c == 22; }");

	TEST(ir_ret,
			"type $tstruct = {i1,i2,i2,i8}"
			"$f = i4($tstruct* $a, [i4 x 3]* $b){"
			"  $a_p = elem $a, i8 2"
			"  $a_x = load $a_p"
			"  $a_z = zext i4, $a_x"
			"  $b_p = elem $b, i8 1"
			"  $b_x = load $b_p"
			"  $c = xor $a_z, $b_x"
			"	ret $c"
			"}",
			3 ^ 6,
			&target,
			"typedef struct { char c; short a, b; long l; } A;"
			"extern int f(const A *, int [3]);"
			"int main() { A a = { 1, 2, 3, 4 }; int b[] = { 5, 6, 7 }; return f(&a, &b); }");

	TEST(ir_ret,
			"$x = i4 internal 3"
			"$f = i4() internal"
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

	TEST(ir_ret,
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

	TEST(ir_ret,
			"$delegate = i4(i4()* $f) internal"
			"{"
			"	$x = call $f()"
			"	ret $x"
			"}"
			"$ret4 = i4() internal { ret i4 4 }"
			"$entry = i4()"
			"{"
			"	$x = call $delegate($ret4)"
			"	ret $x"
			"}",
			4,
			&target,
			NULL);

	TEST(ir_ret,
			"$x = i4 internal 0\n"
			"$f = i4(i4 $a, i4 $b, i4 $c, i4 $d) internal\n"
			"{\n"
			"  $a_ = eq $a, i4 9\n"
			"  br $a_, $next1, $abort\n"
			"$next1:\n"
			"  $b_ = eq $b, i4 2\n"
			"  br $b_, $next2, $abort\n"
			"$next2:\n"
			"  $c_ = eq $c, i4 7\n"
			"  br $b_, $next3, $abort\n"
			"$next3:\n"
			"  $d_ = eq $d, i4 3\n"
			"  br $b_, $next4, $abort\n"
			"$next4:\n"
			"  ret i4 0\n"
			"$abort:\n"
			"  ret i4 1\n"
			"}\n"
			"$entry = i4()\n"
			"{\n"
			"  $a_ = load $x\n"
			"  $a = add $a_, i4 9\n"
			"  $b_ = load $x\n"
			"  $b = add $b_, i4 2\n"
			"  $c_ = load $x\n"
			"  $c = add $c_, i4 7\n"
			"  $d_ = load $x\n"
			"  $d = add $d_, i4 3\n"
			"  \n"
			"  $r = call $f($a, $b, $c, $d)\n" /* shouldn't mix up registers when assigning */
			"  \n"
			"  ret $r\n"
			"}\n",
			0,
			&target,
			NULL);

	TEST(ir_ret,
			"$psub = i8(i4* $a, i4* $b){"
			"	$diff = ptrsub $a, $b"
			"	ret $diff"
			"}"
			"$sub = i4(i4 $a, i4 $b){"
			"	$diff = sub $a, $b"
			"	ret $diff"
			"}"
			"$entry = i4(){"
			"	$ap = int2ptr i4*, i4 24"
			"	$bp = int2ptr i4*, i4 28"
			"	$diffp = call $psub($bp, $ap)"
			"	$diff = call $sub(i4 5, i4 3)"
			"	$diff_shift = shiftl $diff, i4 4"
			"	$diffp_cast = trunc i4, $diffp"
			"	$diff_ret = or $diff_shift, $diffp_cast"
			"	ret $diff_ret"
			"}",
			((5-3) << 4) | 1,
			&target,
			NULL);

	TEST(ir_ret,
			"$x = i4 internal 3"
			"$f = i4() internal"
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

	TEST(ir_ret,
			"$d = i4(i4 $a){"
			"	$x = sdiv $a, i4 2"
			"	ret $x"
			"}"
			"$entry = i4()"
			"{"
			"  $x = call $d(i4 -10)"
			"  ret $x"
			"}",
			-5,
			&target,
			NULL);

	TEST(ir_ret,
			"$memset = i1*(...)"
			"type $T = { i8, i8, i8, i4 }"
			"$entry = i4() {"
			"	$a = alloca $T"
			"	$b = alloca $T"
			"	$_1 = call $memset($b, i1 0xff, i8 28)"
			"	$t0 = elem $a, i8 0"
			"	$t1 = elem $a, i8 1"
			"	$t2 = elem $a, i8 2"
			"	$t3 = elem $a, i8 3"
			"	store $t0, i8 0"
			"	store $t1, i8 1"
			"	store $t2, i8 2"
			"	store $t3, i4 3"
			"	memcpy $b, $a"
			"	$x0 = elem $b, i8 3"
			"	$x0_ = load $x0"
			"	ret $x0_"
			"}",
			3,
			&target,
			NULL);

	TEST(ir_ret,
			"$g = i4(...){ret i4 3}"
			"$f = i4(i4 $x){"
			"	jmp $blk"
			"$blk:"
			"	$g_ = call $g(i4 1)" /* ensure clobber of arg register */
			"	$s = add $x, $g_" /* $x should be spilt */
			"	ret $s"
			"}"
			"$entry = i4(){"
			"  $x = call $f(i4 7)"
			"  ret $x"
			"}",
			10,
			&target,
			NULL);

	TEST(ir_emit,
			"$f = i4(i4 $a){"
			"  ret i4 3"
			"}",
			"mov $3, %eax",
			&target);

	TEST(ir_emit,
			"$x = {i4,i2} global aliasinit i2 3",
			".word 0x3",
			&target);

	TEST(ir_emit,
			"$vf = void()"
			"$f = i4(){"
			"  $x = call $vf()"
			"  ret i4 0"
			"}",
			"mov $0, %eax",
			&target);

	TEST(ir_emit,
			"$gi = i2"
			"$gp = i2*"
			"$test = void() { store $gp, $gi ret void }",
			"lea gi(%rip), %rax\nmov %rax, gp(%rip)",
			&target_nomangle);

	TEST(ir_error,
			"$main = i4(){\n"
			"	$z = alloca i4()\n"
			"	ret i4 0\n"
			"}\n",
			&target,
			"alloca of function type", 1,
			(char *)NULL);

	TEST(ir_compiles,
			"$f = i4*(){"
			"	$p = alloca i4"
			"	$plus = ptradd $p, i8 3"
			"	ret $plus"
			"}",
			&target);

	TEST(ir_compiles,
			"$test = i4()"
			"{"
			"	jmp $l"
			"	ret i4 3"
			"$l:"
			"	ret i4 1"
			"}",
			&target);

	TEST(ir_compiles,
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

	TEST(ir_compiles,
			"$g = i4()"
			"$f = i4() { $x = call $g() ret $x }"
			"$g = i4() { $x = call $f() ret $x }",
			&target);

	TEST(ir_assembles,
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

	TEST(ir_assembles,
			"$f = i1(i4 $a){"
			"	$y = gt $a, i4 2" /* check x86's cmp $literal, %reg */
			"	$x = gt i4 2, $a"
			"	$p = add $x, $y"
			"	ret $p"
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
