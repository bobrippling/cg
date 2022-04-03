#ifndef VAL_H
#define VAL_H

#include <stdbool.h>

#include "macros.h"
#include "reg.h"

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

val *val_new_label(char *, struct type *) attr_nonnull();

val *val_new_local(char * /*consumed*/, struct type *, bool is_alloca)
	attr_nonnull((2));

val *val_new_localf(
		struct type *, bool alloca,
		const char *fmt, ...)
	attr_nonnull();

val *val_new_argument(
		char * /*consumed*/,
		struct type *ty)
	attr_nonnull();

val *val_new_reg(regt, struct type *);
val *val_new_stack(int stack_off, struct type *);

void val_temporary_init(val *, struct type *);

bool val_is_reg(val *);
bool val_is_reg_specific(val *, regt);
bool val_on_stack(val *);
bool val_can_be_assigned_reg(val *);
bool val_can_be_assigned_mem(val *);
unsigned val_size(val *);
void val_size_align(val *, unsigned *, unsigned *);
struct type *val_type(val *);

struct location *val_location(val *);


/* --- util */
#define VAL_STR_SZ 64

char *val_str(val *);
char *val_str_r(char buf[], val *);
char *val_str_rn(unsigned bufindex, val *);

const char *val_frontend_name(val *);

unsigned val_hash(val *);

bool val_is_mem(val *);
bool val_is_int(val *, size_t *);
bool val_is_volatile(val *); /* e.g. reg */
bool val_is_undef(val *);
bool val_is_abi(val *);

enum operand_category val_operand_category(val *v, bool dereference);
bool val_operand_category_matches(enum operand_category, enum operand_category);

#endif
