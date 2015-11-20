#ifndef TOKENISE_H
#define TOKENISE_H

#include <stdbool.h>
#include <stdio.h>
#include "macros.h"
#include "op.h"

#define TOKENS        \
	OTHER(int)          \
	OTHER(ident)        \
	OTHER(bareword)     \
	OTHER(eof)          \
	OTHER(unknown)      \
	OTHER(string)       \
	PUNCT(lparen, '(')  \
	PUNCT(rparen, ')')  \
	PUNCT(lbrace, '{')  \
	PUNCT(rbrace, '}')  \
	PUNCT(lsquare, '[') \
	PUNCT(rsquare, ']') \
	PUNCT(dot, '.')     \
	PUNCT(comma, ',')   \
	PUNCT(equal, '=')   \
	PUNCT(colon, ':')   \
	PUNCT(semi, ';')    \
	PUNCT(star, '*')    \
	PUNCTSTR(arrow, "->")\
	KW(load)            \
	KW(store)           \
	KW(alloca)          \
	KW(elem)            \
	KW(ptradd)          \
	KW(ptr2int)         \
	KW(int2ptr)         \
	KW(ptrcast)         \
	KW(ret)             \
	KW(zext) KW(sext)   \
	KW(br)              \
	KW(jmp)             \
	KW(call)            \
	KW(i1) KW(i2)       \
	KW(i4) KW(i8)       \
	KW(f4) KW(f8)       \
	KW(flarge)          \
	KW(void)            \
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
#define PUNCTSTR(t, s) tok_ ## t,
#define OP(t) tok_ ## t,
#define CMP(t) tok_ ## t,
	TOKENS
#undef CMP
#undef OP
#undef PUNCTSTR
#undef PUNCT
#undef OTHER
#undef KW
};

tokeniser *token_init(FILE *, const char *fname);
void token_fin(tokeniser *, int *err);

const char *token_to_str(enum token);

enum token token_next(tokeniser *);
enum token token_peek(tokeniser *);
bool token_accept(tokeniser *, enum token);

int token_last_int(tokeniser *);
char *token_last_ident(tokeniser *);
char *token_last_bareword(tokeniser *);
char *token_last_string(tokeniser *, size_t *const);

void token_curline(tokeniser *, char *out, size_t len, size_t *const off);
unsigned token_curlineno(tokeniser *);
const char *token_curfile(tokeniser *);

int token_is_op(enum token, enum op *);
int token_is_cmp(enum token, enum op_cmp *);

#endif
