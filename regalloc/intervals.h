#ifndef INTERVALS_H
#define INTERVALS_H

struct isn;
struct function;
struct target;
struct interval;

void intervals_create(
		dynarray *intervals,
		dynmap *lf_map,
		struct isn *isn_first,
		struct function *,
		const struct target *);

void interval_delete(struct interval *);
void intervals_delete(dynarray *intervals);

#endif
