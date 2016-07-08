#ifndef MACROS_H
#define MACROS_H

#define countof(x) (sizeof(x) / sizeof*(x))
#define const_cast(T, v) (T)(v)
#define static_assert(e, s) typedef char s[(e) ? 1 : -1]

#ifdef __GNUC__
#  define attr_printf(x, y) __attribute__((format(printf, x, y)))
#  define attr_warn_unused __attribute__((warn_unused_result))
#  define attr_nonnull(...) __attribute__((nonnull __VA_ARGS__))
#  define attr_static static
#else
#  define attr_printf(x, y)
#  define attr_warn_unused
#  define attr_nonnull(...)
#  define attr_static
#endif

#ifndef MIN
#  define MIN(x, y) ((x) < (y) ? (x) : (y))
#  define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif

#endif
