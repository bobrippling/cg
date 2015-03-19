#ifndef TOKENISE_H
#define TOKENISE_H

#include <stdio.h>
#include "macros.h"
#include "op.h"

#define TOKENS        \
	OTHER(int)          \
	OTHER(ident)        \
	OTHER(eof)          \
	OTHER(unknown)      \
	PUNCT(lparen, '(')  \
	PUNCT(rparen, ')')  \
	PUNCT(dot, '.')     \
	PUNCT(comma, ',')   \
	PUNCT(equal, '=')   \
	KW(load)            \
	KW(store)           \
	KW(alloca)          \
	KW(elem)            \
	KW(ret)             \
	OP(add)             \
	OP(sub)             \
	OP(mul)             \
	OP(div)             \
	OP(mod)             \
	OP(xor)             \
	OP(or)              \
	OP(and)             \
	OP(or_sc)           \
	OP(and_sc)          \
	OP(shiftl)          \
	OP(shiftr)          \
	OP(shiftra)

typedef struct tokeniser tokeniser;

enum token
{
#define KW(t) tok_ ## t,
#define OTHER KW
#define PUNCT(t, c) tok_ ## t,
#define OP(t) tok_ ## t,
	TOKENS
#undef OP
#undef PUNCT
#undef OTHER
#undef KW
};

tokeniser *token_init(FILE *);
void token_fin(tokeniser *, int *err);

const char *token_to_str(enum token);

enum token token_next(tokeniser *);

int token_last_int(tokeniser *);
char *token_last_ident(tokeniser *);

void token_curline(tokeniser *, char *out, size_t len);

int token_is_op(enum token, enum op *);

#endif
