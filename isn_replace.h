#ifndef ISN_REPLACE_H
#define ISN_REPLACE_H

#include <stdbool.h>

#include "reg.h"
#include "macros.h"

struct val;
struct isn;
struct block;
struct function;

enum replace_mode
{
	REPLACE_INPUTS = 1 << 0,
	REPLACE_OUTPUTS = 1 << 1
};

void isn_replace_uses_with_load_store(
		struct val *old, struct val *spill, struct isn *, struct function *);

void isn_replace_val_with_val(
		struct isn *,
		struct val *old,
		struct val *new,
		enum replace_mode mode);

void isn_vals_get(
		struct isn *i,
		struct val *inputs[attr_static 2],
		struct val **const outputs);

bool isn_vals_has(struct isn *i, struct val *);

#endif
