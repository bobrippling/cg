#ifndef PASS_ISEL_H
#define PASS_ISEL_H

struct target;
void pass_isel(function *, struct unit *, const struct target *);

#endif
