#ifndef VAL_H
#define VAL_H

#include "macros.h"

struct type;
struct variable;
struct global;

typedef struct val val;

val *val_new_i(int i, struct type *);

/* refer to a local, arg or global */
val *val_new_global(struct global *) attr_nonnull;
val *val_new_argument(struct variable *) attr_nonnull;
val *val_new_local(struct variable *); /* maybe null */

unsigned val_size(val *);
struct type *val_type(val *);

/* --- util */
#define VAL_STR_SZ 32

char *val_str(val *);
char *val_str_r(char buf[], val *);
char *val_str_rn(unsigned bufindex, val *);

unsigned val_hash(val *);

#endif
