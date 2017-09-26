#ifndef PARSE_H
#define PARSE_H

#include <stdarg.h>

#include "unit.h"

typedef attr_printf(4, 0) void parse_error_fn(
		const char *file,
		int line,
		void *ctx,
		const char *fmt,
		va_list);

unit *parse_code(tokeniser *tok, int *const err, const struct target *target);

unit *parse_code_cb(tokeniser *tok, const struct target *target, parse_error_fn, void *);

#endif
