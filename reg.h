#ifndef REG_H
#define REG_H

typedef unsigned regt;

#define regt_index(x) (x & 0xffff)
#define regt_is_int(x) (!(x & 0x10000))
#define regt_is_fp(x) (!!(x & 0x10000))
#define regt_make(index, is_fp) ((index) | (!!(is_fp) << (2 * 8)))
#define regt_hash(x) (x)
#define regt_make_invalid() (-1u)
#define regt_is_valid(r) ((r) != -1u)

#endif
