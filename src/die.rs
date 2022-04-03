#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include "die.h"

void die(const char *fmt, ...)
{
	va_list l;
	va_start(l, fmt);
	vfprintf(stderr, fmt, l);
	va_end(l);

	if(*fmt && fmt[strlen(fmt)-1] == ':'){
		fprintf(stderr, " %s\n", strerror(errno));
	}else{
		fputc('\n', stderr);
	}

	exit(1);
}
