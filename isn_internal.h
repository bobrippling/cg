#ifndef ISN_INTERNAL_H
#define ISN_INTERNAL_H

struct block;
struct val;

void isn_free_r(struct isn *);

void isn_dump(struct isn *, struct block *);

void isn_on_live_vals(
		struct isn *,
		void (struct val *, struct isn *, void *),
		void *);

void isn_on_all_vals(
		struct isn *,
		void (struct val *, struct isn *, void *),
		void *);

#endif
