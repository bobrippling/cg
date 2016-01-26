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
