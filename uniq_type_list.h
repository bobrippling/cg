#ifndef UNIQ_TYPE_LIST_H
#define UNIQ_TYPE_LIST_H

struct uniq_type_list;

void uniq_type_list_init(
		struct uniq_type_list *us,
		unsigned ptrsz, unsigned ptralign);

void uniq_type_list_free(struct uniq_type_list *);

#endif
