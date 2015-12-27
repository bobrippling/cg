#ifndef MACROS_H
#define MACROS_H

#define countof(x) (sizeof(x) / sizeof*(x))

#ifdef __GNUC__
#  define attr_printf(x, y) __attribute__((format(printf, x, y)))
#  define attr_warn_unused __attribute__((warn_unused_result))
#  define attr_nonnull __attribute__((nonnull))
#else
#  define attr_printf(x, y)
#  define attr_warn_unused
#  define attr_nonnull
#endif

#endif
