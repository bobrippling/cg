#ifndef X86_H
#define X86_H

#include "global.h"
#include "target.h"

void x86_out(struct unit *, global *, void *);

extern const struct target_arch_isn backend_isns_x64[];

/* XXX: temporary */
extern const struct backend_isn x86_isn_mov;
extern const struct backend_isn x86_isn_lea;
extern const struct backend_isn x86_isn_imul;
extern const struct backend_isn x86_isn_add;
extern const struct backend_isn x86_isn_movzx;
extern const struct backend_isn x86_isn_cmp;
extern const struct backend_isn x86_isn_set;
extern const struct backend_isn x86_isn_test;
extern const struct backend_isn x86_isn_call;

#endif
