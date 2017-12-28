#ifndef REGS_H
#define REGS_H

/* TODO? s/dynarray/dynarray_(bool)/ */

void free_regs_create(dynarray *, const struct target *);
void free_regs_delete(dynarray *);

void free_regs_merge(dynarray *dest, dynarray *src1, dynarray *src2);
unsigned free_regs_any(dynarray *);

unsigned free_regs_available(dynarray *);

#endif
