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
		char *nl, *got;
		size_t nr;

		got = fgets(buf + off, l - off, f);

		if(!got || (nr = strlen(buf + off)) == 0){
			const int e = errno;
			if(off == 0){
				free(buf);
				buf = NULL;
			}
			errno = e;
			return buf;
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

int cat_file(FILE *from, FILE *to)
{
	char buf[512];
	size_t n;

	if(fseek(from, SEEK_SET, 0))
		return -1;

	while((n = fread(buf, 1, sizeof buf, from)))
		if(fwrite(buf, 1, n, to) != n)
			return -1;

	return 0;
}
