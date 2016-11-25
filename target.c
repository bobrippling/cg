#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <sys/utsname.h>

#include "target.h"

#include "mem.h"
#include "macros.h"
#include "die.h"

#include "x86.h"

static const unsigned scratch_regs_x64[] = {
	regt_make(0, 0), /* eax */
	regt_make(2, 0), /* ecx */
	regt_make(3, 0), /* edx */
	regt_make(4, 0), /* esi */
	regt_make(5, 0)  /* edi */
};

static const unsigned callee_saves_x64[] = {
	regt_make(1, 0) /* rbx */
};

static const unsigned arg_regs_x64[] = {
	regt_make(4, 0), /* rdi */
	regt_make(5, 0), /* rsi */
	regt_make(3, 0), /* rdx */
	regt_make(2, 0)  /* rcx */
	/* TODO: r8, r9 */
};

static const unsigned ret_regs_x64[] = {
	regt_make(0, 0), /* rax */
	regt_make(3, 0)  /* rdx */
};

#define ARCH_ABI(arch)             \
	{                                \
		{                              \
			scratch_regs_##arch,         \
			countof(scratch_regs_##arch) \
		},                             \
		{                              \
			callee_saves_##arch,         \
			countof(callee_saves_##arch) \
		},                             \
		{                              \
			arg_regs_##arch,             \
			countof(arg_regs_##arch)     \
		},                             \
		{                              \
			ret_regs_##arch,             \
			countof(ret_regs_##arch)     \
		},                             \
	}

static const struct
{
	const char *name;

	struct target_arch arch;
	struct target_abi abi;
	global_emit_func *emit;

} arches[] = {
	{
		"ir",
		{ 8, 8 },
		{
			0
		},
		global_dump
	},
	{
		"x86_64",
		{ 8, 8 },
		ARCH_ABI(x64),
		x86_out
	}
	/* TODO: i386 */
};

static const struct
{
	const char *name;
	struct target_sys sys;
} systems[] = {
	{
		"linux",
		{
			".L",
			".rodata",
			".weak",
			".weak",
			false /* align_is_pow2 */
		}
	},
	{
		"darwin",
		{
			"L",
			".section __TEXT,__const",
			".weak_reference",
			".weak_definition",
			true /* align_is_pow2 */
		}
	},
};

static bool maybe_add_arch(const char *arch, struct target *out)
{
	size_t i;
	for(i = 0; i < countof(arches); i++){
		if(!strcmp(arch, arches[i].name)){
			memcpy(&out->arch, &arches[i].arch, sizeof out->arch);
			memcpy(&out->abi, &arches[i].abi, sizeof out->abi);
			out->emit = arches[i].emit;
			return true;
		}
	}
	return false;
}

static bool maybe_add_sys(const char *sys, struct target *out)
{
	size_t i;
	for(i = 0; i < countof(systems); i++){
		if(!strcmp(sys, systems[i].name)){
			memcpy(&out->sys, &systems[i].sys, sizeof out->sys);
			return true;
		}
	}
	return false;
}

static void filter_unam(struct utsname *unam)
{
	char *p;

	if(!strcmp(unam->machine, "amd64"))
		strcpy(unam->machine, "x86_64");

	for(p = unam->sysname; *p; p++)
		*p = tolower(*p);
}

void target_default(struct target *target)
{
	struct utsname unam;

	if(uname(&unam))
		die("uname:");

	filter_unam(&unam);

	memset(target, 0, sizeof *target);

	if(!maybe_add_arch(unam.machine, target))
		die("couldn't parse machine '%s'", unam.machine);

	if(!maybe_add_sys(unam.sysname, target))
		die("couldn't parse sysname '%s'", unam.sysname);
}

void target_parse(const char *const triple, struct target *out)
{
	char *const triple_dup = xstrdup(triple);
	char *p, *strtok_ctx;

	target_default(out);

	for(p = strtok_r(triple_dup, "-", &strtok_ctx);
			p;
			p = strtok_r(NULL, "-", &strtok_ctx))
	{
		bool found_arch = maybe_add_arch(p, out);
		bool found_sys = maybe_add_sys(p, out);

		if(!found_arch && !found_sys){
			die("couldn't parse target entry '%s'", p);
		}
	}

	free(triple_dup);
}
