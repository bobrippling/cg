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

static const unsigned scratch_regs_armv7l[] = {
	/* r0-r3 */
	regt_make(0, 0),
	regt_make(1, 0),
	regt_make(2, 0),
	regt_make(3, 0)
};

static const unsigned callee_saves_armv7l[] = {
};

static const unsigned arg_regs_armv7l[] = {
	/* r0-r3 */
	regt_make(0, 0),
	regt_make(1, 0),
	regt_make(2, 0),
	regt_make(3, 0)
};

static const unsigned ret_regs_armv7l[] = {
	/* r0-r1 */
	regt_make(0, 0),
	regt_make(1, 0)
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
	on_global_func *emit;

} arches[] = {
	{
		"ir",
		{ { 8, 8 }, backend_isns_x64, true },
		/* arbitrary ABI
		 * this should be configurable to emit abi'd IR for a certain arch */
		ARCH_ABI(x64),
		global_dump
	},
	{
		"x86_64",
		{ { 8, 8 }, backend_isns_x64, true },
		ARCH_ABI(x64),
		x86_out
	},
	{
		"armv7l",
		{ 4, 4, backend_isns_armv7l, false },
		ARCH_ABI(armv7l),
		global_dump /* TODO */
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
			false, /* align_is_pow2 */
			false, /* leading_underscore */
			{ /* pic */
				false, /* active */
				true /* call_requires_plt */
			}
		}
	},
	{
		"darwin",
		{
			"L",
			".section __TEXT,__const",
			".weak_reference",
			".weak_definition",
			true, /* align_is_pow2 */
			true, /* leading_underscore */
			{ /* pic */
				true, /* active */
				false /* call_requires_plt */
			}
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
	bool found_1_arch = false, found_1_sys = false;

	memset(out, 0, sizeof(*out));

	for(p = strtok_r(triple_dup, "-", &strtok_ctx);
			p;
			p = strtok_r(NULL, "-", &strtok_ctx))
	{
		bool found_arch = maybe_add_arch(p, out);
		bool found_sys = maybe_add_sys(p, out);

		if(!found_arch && !found_sys){
			die("couldn't parse target entry '%s'", p);
		}

		found_1_arch |= found_arch;
		found_1_sys |= found_sys;
	}

	if(!found_1_arch || !found_1_sys){
		die("couldn't parse target into sys + arch: '%s'", triple);
	}

	free(triple_dup);
}
