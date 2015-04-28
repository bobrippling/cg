#ifndef VAL_H
#define VAL_H

#include "block.h"

typedef struct val val;

val *val_new_i(int i, unsigned sz);
val *val_new_ptr_from_int(int);

val *val_new_lbl(char * /*consumed*/);

val *val_make_alloca(block *, int n, unsigned elemsz);

val *val_element(
		block *, val *lval,
		int i, unsigned elemsz,
		char *ident_to); /* i'th element */

val *val_element_noop(val *lval, int i, unsigned elemsz);

void val_store(block *, val *rval, val *lval);
val *val_load(block *, val *, unsigned size);

val *val_add(block *, val *, val *);
val *val_equal(block *, val *, val *);

/* TODO: sext and trunc */
val *val_zext(block *, val *, unsigned);

void val_ret(block *, val *);

/* anonymous */
val *val_alloca(void);
val *val_name_new(unsigned sz, char *ident);

/* util */
#define VAL_STR_SZ 32

char *val_str(val *);
char *val_str_r(char buf[], val *);
char *val_str_rn(unsigned buf_n, val *);

unsigned val_hash(val *);

#endif
