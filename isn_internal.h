#ifndef ISN_INTERNAL_H
#define ISN_INTERNAL_H

struct block;
struct val;

typedef struct isn isn;

void isn_free_r(isn *);

void isn_dump(isn *, struct block *);

void isn_on_live_vals(isn *, void (struct val *, isn *, void *), void *);
void isn_on_all_vals( isn *, void (struct val *, isn *, void *), void *);

#endif
