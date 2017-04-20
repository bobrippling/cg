#ifndef PASS_SPILL_H
#define PASS_SPILL_H

struct target;
void pass_spill(function *, struct unit *, const struct target *);

void spill_assign(struct val *spill, unsigned *const spill_space);

#endif
