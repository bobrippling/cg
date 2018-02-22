#ifndef X86_H
#define X86_H

#include "global.h"
#include "target.h"

void x86_out(struct unit *, global *, FILE *);

extern const struct target_arch_isn backend_isns_x64[];

#endif
