#ifndef X86_ISN_H
#define X86_ISN_H

#include "x86_isns.h"

#ifdef TODO
struct x86_isn
{
	struct backend_isn *backend_isn;
	int variant; /* index into backend_isn->constraints[] */
};
#endif

struct emit_isn_operand
{
	struct val *val;
	bool dereference;
};
typedef struct emit_isn_operand emit_isn_operand;

#endif
