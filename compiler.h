#ifndef STRBUF_COMPILER_H
#define STRBUF_COMPILER_H

#ifdef __GNUC__
#  define strbuf_compiler_printf(x, y) __attribute__((format(printf, x, y)))
#else
#  define strbuf_compiler_printf(x, y)
#endif

#endif
