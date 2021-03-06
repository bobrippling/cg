#ifndef TOKENISE_H
#define TOKENISE_H

#include <stdbool.h>
#include <stdio.h>
#include "macros.h"
#include "op.h"
#include "string.h"

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
	PUNCTSTR(ellipses, "...") \
	KW(load)            \
	KW(store)           \
	KW(alloca)          \
	KW(elem)            \
	KW(ptradd)          \
	KW(ptrsub)          \
	KW(ptr2int)         \
	KW(int2ptr)         \
	KW(ptrcast)         \
	KW(memcpy)          \
	KW(ret)             \
	KW(zext) KW(sext)   \
	KW(trunc)           \
	KW(br)              \
	KW(jmp)             \
	KW(label)           \
	KW(call)            \
	KW(i1) KW(i2)       \
	KW(i4) KW(i8)       \
	KW(f4) KW(f8)       \
	KW(flarge)          \
	KW(void)            \
	KW(undef)           \
	KW(global)          \
	KW(internal)        \
	KW(aliasinit)       \
	KW(type)            \
	KW(asm)             \
	OP(add)             \
	OP(sub)             \
	OP(mul)             \
	OP(sdiv)            \
	OP(smod)            \
	OP(udiv)            \
	OP(umod)            \
	OP(xor)             \
	OP(or)              \
	OP(and)             \
	OP(shiftl)          \
	OP(shiftr_logic)    \
	OP(shiftr_arith)    \
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
tokeniser *token_init_str(const char *);
void token_fin(tokeniser *, int *err);

const char *token_to_str(enum token);

enum token token_next(tokeniser *);
enum token token_peek(tokeniser *);
bool token_accept(tokeniser *, enum token);

int token_last_int(tokeniser *);
char *token_last_ident(tokeniser *);
const char *token_last_ident_peek(tokeniser *);
char *token_last_bareword(tokeniser *);
void token_last_string(tokeniser *, struct string *);

void token_curline(tokeniser *, char *out, size_t len, size_t *const off);
unsigned token_curlineno(tokeniser *);
const char *token_curfile(tokeniser *);

int token_is_op(enum token, enum op *);
int token_is_cmp(enum token, enum op_cmp *);

#endif
