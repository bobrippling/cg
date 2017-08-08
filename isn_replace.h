#ifndef ISN_REPLACE_H
#define ISN_REPLACE_H

#include "reg.h"

struct val;
struct isn;
struct block;

enum replace_mode
{
	REPLACE_INPUTS = 1 << 0,
	REPLACE_OUTPUTS = 1 << 1
};

void isn_replace_uses_with_load_store(
		struct val *old, struct val *spill, struct isn *, struct block *);

void isn_replace_val_with_val(
		struct isn *,
		struct val *old,
		struct val *new,
		enum replace_mode mode);

#endif
