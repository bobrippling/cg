#ifndef TYPE_ITER_H
#define TYPE_ITER_H

typedef struct type_iter type_iter;

type_iter *type_iter_new(type *);
void type_iter_free(type_iter *);

type *type_iter_next(type_iter *);

#endif
