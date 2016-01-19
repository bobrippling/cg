#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <sys/utsname.h>

#include "target.h"

#include "mem.h"
#include "macros.h"
#include "die.h"

#include "x86.h"

static const struct
{
	const char *name;

	struct target_arch arch;
	global_emit_func *emit;

} arches[] = {
	{ "ir",     { 8, 8 }, global_dump },
	{ "x86_64", { 8, 8 }, x86_out },
};

static const struct
{
	const char *name;
	struct target_sys sys;
} systems[] = {
	{ "linux", { ".L", ".rodata" } },
	{ "darwin",{  "L", ".section __TEXT,__const" } },
};

static bool maybe_add_arch(const char *arch, struct target *out)
{
	size_t i;
	for(i = 0; i < countof(arches); i++){
		if(!strcmp(arch, arches[i].name)){
			memcpy(&out->arch, &arches[i].arch, sizeof out->arch);
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
