#ifndef ISN_REPLACE_H
#define ISN_REPLACE_H

struct val;
struct isn;

void isn_replace_uses_with_load_store(
		struct val *old, struct val *spill, struct isn *);

#endif
