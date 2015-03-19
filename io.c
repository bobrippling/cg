#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "mem.h"
#include "io.h"

char *read_line(FILE *f)
{
	size_t l = 256;
	char *buf = xmalloc(l);
	size_t off = 0;

	for(;;){
		char *nl;
		size_t nr;

		nr = fread(buf + off, 1, l - off, f);
		if(nr == 0){
			const int e = errno;
			free(buf);
			errno = e;
			return NULL;
		}

		nl = memchr(buf + off, '\n', nr);
		if(nl){
			/* found a newline, break for now */
			break;
		}

		off += nr;

		l += 256;
		buf = xrealloc(buf, l);
	}

	return buf;
}
