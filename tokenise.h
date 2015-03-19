#ifndef TOKENISE_H
#define TOKENISE_H

#include <stdio.h>
#include "macros.h"

#define TOKENS   \
	OTHER(int)     \
	OTHER(ident)   \
	OTHER(eof)     \
	OTHER(unknown) \
	KW(load)       \
	KW(store)      \
	KW(alloca)     \
	KW(elem)       \
	KW(add)        \
	KW(ret)

typedef struct tokeniser tokeniser;

enum token
{
#define KW(t) tok_ ## t,
#define OTHER KW
	TOKENS
#undef OTHER
#undef KW
};

tokeniser *token_init(FILE *);
void token_fin(tokeniser *, int *err);

const char *token_to_str(enum token);

enum token token_next(tokeniser *);

int token_last_int(tokeniser *);
const char *token_last_ident(tokeniser *);

#endif
