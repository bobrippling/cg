#include <assert.h>

#include "imath.h"

unsigned log2i(unsigned u)
{
	unsigned pow = 0;

	assert(u > 0);

	while(u){
		pow++;
		u >>= 1;
	}

	return pow - 1;
}

unsigned gap_for_alignment(unsigned current, unsigned align)
{
	if(current == 0)
		return 0;

	if(current < align)
		return align - current;
	else
		return current % align;
}
