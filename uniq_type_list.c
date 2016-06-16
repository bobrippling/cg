#include <stdlib.h>
#include <assert.h>

#include "macros.h"
#include "dynmap.h"

#include "type.h"
#include "type_free.h"

#include "uniq_type_list.h"
#include "uniq_type_list_struct.h"

void uniq_type_list_init(
		struct uniq_type_list *us, unsigned ptrsz, unsigned ptralign)
{
	us->ptrsz = ptrsz;
	us->ptralign = ptralign;
}

void uniq_type_list_free(uniq_type_list *utl)
{
	type *t;
	size_t i;

	/* free everything but the top levels - those
	 * in uniq_type_list */
	for(i = 0; i < countof(utl->primitives); i++)
		type_free_r(utl->primitives[i]);
	type_free_r(utl->tvoid);
	type_free_dynarray_r(&utl->structs);

	for(i = 0; i < countof(utl->primitives); i++)
		type_free_1(utl->primitives[i]);
	type_free_1(utl->tvoid);
	type_free_dynarray_1(&utl->structs);

	for(i = 0; (t = dynmap_value(type *, utl->aliases, i)); i++)
		type_free_1(t); /* this frees 'spel' */

	dynmap_free(utl->aliases);
}
