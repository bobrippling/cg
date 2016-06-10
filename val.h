#ifndef VAL_H
#define VAL_H

#include <stdbool.h>

#include "macros.h"

struct type;
struct variable;
struct global;
struct uniq_type_list;
struct function;

typedef struct val val;

val *val_new_i(int i, struct type *);
val *val_new_void(struct uniq_type_list *);
val *val_new_undef(struct type *);

/* refer to a local, arg or global */
val *val_new_global(struct uniq_type_list *, struct global *) attr_nonnull();

val *val_new_local(char * /*consumed*/, struct type *, bool is_alloca)
	attr_nonnull((2));

val *val_new_argument(
		char * /*consumed*/, int idx,
		struct type *ty,
		struct function *)
	attr_nonnull();

val *val_new_abi_reg(int rno, struct type *);

void val_temporary_init(val *, struct type *);

unsigned val_size(val *);
void val_size_align(val *, unsigned *, unsigned *);
struct type *val_type(val *);

/* --- util */
#define VAL_STR_SZ 32

char *val_str(val *);
char *val_str_r(char buf[], val *);
char *val_str_rn(unsigned bufindex, val *);

unsigned val_hash(val *);

bool val_is_mem(val *);
bool val_is_int(val *, size_t *);
bool val_is_volatile(val *); /* e.g. reg */

#endif
