#ifndef DYNMAP_H
#define DYNMAP_H

typedef struct dynmap dynmap;
/* 0 for match, non-zero for mismatch */
typedef int dynmap_cmp_f(void *, void *);
typedef unsigned dynmap_hash_f(const void *);

dynmap *dynmap_nochk_new(dynmap_cmp_f, dynmap_hash_f);
void    dynmap_free(dynmap *);

void *dynmap_nochk_get(dynmap *, void *key);
void *dynmap_nochk_set(dynmap *, void *key, void *val);

/* keys may be null, so: */
int dynmap_nochk_exists(dynmap *, void *key);

void *dynmap_nochk_key(dynmap *map, int i);

void *dynmap_nochk_value(dynmap *map, int i);

void *dynmap_nochk_rm(dynmap *, void *key);

void dynmap_dump(dynmap *);

/* handy */
unsigned dynmap_strhash(const char *);

#include "dyn.h"

#define NO_EVAL(expr) (void)(0 ? (expr) : 0)

/* don't eval the hash functions, but put a call to it for warning's sake
 * can't call the cmpf, since it may be NULL and we get a compile time error
 */
#define dynmap_new(type_k, cmpf, hashf)                              \
	((NO_EVAL(hashf((type_k)0))),                                      \
	 dynmap_nochk_new((dynmap_cmp_f *)cmpf, (dynmap_hash_f *)hashf))

#define dynmap_get(type_k, type_v, map, key)   \
	(UCC_TYPECHECK(type_k, key),                 \
	 (type_v)dynmap_nochk_get(map, (void *)key))

#define dynmap_set(type_k, type_v, map, key, value)   \
	(UCC_TYPECHECK(type_k, key),                        \
	 UCC_TYPECHECK(type_v, value),                      \
	 (type_v)dynmap_nochk_set(map, (void *)key, (void *)value))

#define dynmap_key(type_k, map, idx)   \
	 ((type_k)dynmap_nochk_key(map, idx))

#define dynmap_value(type_v, map, idx)   \
	 ((type_v)dynmap_nochk_value(map, idx))

#define dynmap_exists(type_k, map, key)  \
	(UCC_TYPECHECK(type_k, key),           \
	 dynmap_nochk_exists(map, key))

#define dynmap_rm(type_k, type_v, map, key) \
	(UCC_TYPECHECK(type_k, key),              \
	 (type_v)dynmap_nochk_rm(map, key))

#endif
