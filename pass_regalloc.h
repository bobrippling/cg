#ifndef PASS_REGALLOC_H
#define PASS_REGALLOC_H

struct target;
void pass_regalloc(function *, struct unit *, const struct target *);

#endif