#include <stdlib.h>
#include <assert.h>

#include "mem.h"
#include "macros.h"

#include "type.h"
#include "type_iter.h"

struct type_iter
{
	type **types;
	size_t i, n;
};

attr_nonnull((1, 3))
static void flatten(type *ty, type **outs, size_t *const nout)
{
	if(type_is_struct(ty)){
		size_t i;
		for(i = 0; ; i++){
			type *memb = type_struct_element(ty, i);
			if(!memb)
				break;

			flatten(memb, outs, nout);
		}
	}else{
		if(outs)
			outs[*nout] = ty;

		++*nout;
	}
}

type_iter *type_iter_new(type *t)
{
	type_iter *ti = xcalloc(1, sizeof *ti);
	size_t cnt = 0;
	type **flattypes;

	flatten(t, NULL, &cnt);
	ti->n = cnt;

	flattypes = xcalloc(cnt, sizeof(*flattypes));

	cnt = 0;
	flatten(t, flattypes, &cnt);

	ti->types = flattypes;

	return ti;
}

void type_iter_free(type_iter *ti)
{
	free(ti->types);
	free(ti);
}

type *type_iter_next(type_iter *ti)
{
	if(ti->i >= ti->n)
		return NULL;

	return ti->types[ti->i++];
}
