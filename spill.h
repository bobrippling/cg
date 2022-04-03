#ifndef SPILL_H
#define SPILL_H

struct val;
struct isn;
struct uniq_type_list;
struct function;
struct block;

void spill(
		struct val *,
		struct isn *before_isn,
		struct uniq_type_list *,
		struct function *,
		struct block *);

#endif
