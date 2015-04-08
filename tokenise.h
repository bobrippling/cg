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
	PUNCT(lbrace, '{')  \
	PUNCT(rbrace, '}')  \
	PUNCT(dot, '.')     \
	PUNCT(comma, ',')   \
	PUNCT(equal, '=')   \
	PUNCT(colon, ':')   \
	KW(load)            \
	KW(store)           \
	KW(alloca)          \
	KW(elem)            \
	KW(ret)             \
	KW(zext)            \
	KW(br)              \
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
	OP(shiftra)         \
	CMP(eq)             \
	CMP(ne)             \
	CMP(gt)             \
	CMP(ge)             \
	CMP(lt)             \
	CMP(le)

typedef struct tokeniser tokeniser;

enum token
{
#define KW(t) tok_ ## t,
#define OTHER KW
#define PUNCT(t, c) tok_ ## t,
#define OP(t) tok_ ## t,
#define CMP(t) tok_ ## t,
	TOKENS
#undef CMP
#undef OP
#undef PUNCT
#undef OTHER
#undef KW
};

tokeniser *token_init(FILE *);
void token_fin(tokeniser *, int *err);

const char *token_to_str(enum token);

enum token token_next(tokeniser *);
enum token token_peek(tokeniser *);

int token_last_int(tokeniser *);
char *token_last_ident(tokeniser *);

void token_curline(tokeniser *, char *out, size_t len);

int token_is_op(enum token, enum op *);
int token_is_cmp(enum token, enum op_cmp *);

#endif
