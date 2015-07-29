#ifndef VAL_H
#define VAL_H

#include <stdbool.h>

#include "macros.h"

struct type;
struct variable;
struct global;
struct uniq_type_list;

typedef struct val val;

val *val_new_i(int i, struct type *);
val *val_new_void(struct uniq_type_list *);

/* refer to a local, arg or global */
val *val_new_global(struct uniq_type_list *, struct global *) attr_nonnull();
val *val_new_argument(char * /*consumed*/, int idx, struct type *ty) attr_nonnull();
val *val_new_local(char * /*consumed*/, struct type *) attr_nonnull((2));

unsigned val_size(val *);
struct type *val_type(val *);

/* --- util */
#define VAL_STR_SZ 32

char *val_str(val *);
char *val_str_r(char buf[], val *);
char *val_str_rn(unsigned bufindex, val *);

unsigned val_hash(val *);

bool val_is_mem(val *);

#endif
