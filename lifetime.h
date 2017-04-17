#ifndef LIFETIME_H
#define LIFETIME_H

#include <stdbool.h>

struct function;
struct isn;
struct lifetime;

void lifetime_fill_func(struct function *);
bool lifetime_contains(const struct lifetime *, struct isn *, bool include_last);

#endif
