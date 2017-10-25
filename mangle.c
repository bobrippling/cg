#include "mem.h"

#include "target.h"
#include "mangle.h"

char *mangle(const char *name, char **const out, const struct target *target)
{
	if(*out)
		return *out;

	if(target->sys.leading_underscore){
		size_t len = strlen(name) + 1;
		char *new = xmalloc(len + 1);

		new[0] = '_';
		strcpy(new + 1, name);
		*out = new;
	}else{
		*out = (char *)name;
	}

	return *out;
}

void mangle_free(const char *name, char **out)
{
	if(*out != name)
		free(*out);
	*out = NULL;
}
