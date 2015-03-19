#ifndef MACROS_H
#define MACROS_H

#define countof(x) (sizeof(x) / sizeof*(x))

#ifdef __GNUC__
#  define attr_printf(x, y) __attribute__((format(printf, x, y)))
#endif

#endif
