#ifndef VAL_H
#define VAL_H

#include "block.h"

typedef struct val val;

val *val_new_i(int);
val *val_new_ptr_from_int(int);

val *val_alloca(block *, int n, unsigned elemsz);
val *val_element(block *, val *lval, int i, unsigned elemsz); /* i'th element */
val *val_element_noop(val *lval, int i, unsigned elemsz);

void val_store(block *, val *rval, val *lval);
val *val_load(block *, val *);

val *val_add(block *, val *, val *);
val *val_equal(block *, val *, val *);

void val_ret(block *, val *);

/* anonymous */
val *val_name_new_lval(void);
val *val_name_new(void);

/* util */

char *val_str(val *);
unsigned val_hash(val *);

#endif
