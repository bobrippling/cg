#ifndef X86_ISN_H
#define X86_ISN_H

#include "x86_isns.h"

struct x86_isn
{
	struct backend_isn *backend_isn;
	int variant; /* index into backend_isn->constraints[] */
};

#endif
