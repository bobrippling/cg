#ifndef REG_H
#define REG_H

typedef unsigned regt;

#define regt_index(x) (x & 0xffff)
#define regt_is_int(x) ((x & 0xf0000) == 0)
#define regt_is_fp(x) (!!(x & 0xf0000))
#define regt_make(index, is_fp) ((index) | (!!(is_fp) << (2 * 8)))

#endif
