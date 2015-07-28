#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>

#include "mem.h"
#include "macros.h"
#include "str.h"
#include "io.h"

#include "tokenise.h"

#define TOKEN_PEEK_EMPTY tok_eof

struct tokeniser
{
	FILE *f;
	int eof;
	int ferr;
	unsigned lno;

	const char *fname;
	char *line, *linep;
	enum token unget;

	char *lastident;
	char *lastbareword;
	long lastint;
};

static const struct
{
	const char *kw;
	enum token tok;
} keywords[] = {
#define KW(t) { #t, tok_ ## t },
#define OTHER(t)
#define PUNCT(t, s)
#define OP KW
#define CMP KW
	TOKENS
#undef CMP
#undef OP
#undef PUNCT
#undef OTHER
#undef KW
};

tokeniser *token_init(FILE *f, const char *fname)
{
	tokeniser *t = xmalloc(sizeof *t);
	memset(t, 0, sizeof *t);

	t->f = f;
	t->fname = fname;
	t->unget = TOKEN_PEEK_EMPTY;

	return t;
}

void token_fin(tokeniser *t, int *const err)
{
	if(t->f){
		int failed = fclose(t->f);

		if(!t->ferr && failed)
			t->ferr = errno;
	}

	*err = t->ferr;

	free(t);
}

const char *token_to_str(enum token t)
{
	switch(t){
#define OTHER(x) case tok_ ## x: return #x;
#define KW(x) case tok_ ## x: return "tok_" #x;
#define PUNCT(x, p) case tok_ ## x: return #p;
#define OP OTHER
#define CMP OTHER
		TOKENS
#undef OTHER
#undef KW
#undef PUNCT
#undef OP
#undef CMP
	}
	assert(0);
}

static int consume_word(tokeniser *t, const char *word)
{
	if(!str_beginswith(t->linep, word))
		return 0;

	if(isident(t->linep[strlen(word)], 1))
		return 0;

	t->linep += strlen(word);
	return 1;
}

static char *tokenise_ident(tokeniser *t)
{
	char *end;
	char *buf;
	size_t len;

	for(end = t->linep + 1; isident(*end, 1); end++);

	len = end - t->linep + 1;
	buf = xmalloc(len);
	memcpy(buf, t->linep, len - 1);
	buf[len - 1] = '\0';

	t->linep += len - 1;

	return buf;
}

enum token token_next(tokeniser *t)
{
	size_t i;

	if(t->unget != TOKEN_PEEK_EMPTY){
		enum token unget = t->unget;
		t->unget = TOKEN_PEEK_EMPTY;
		return unget;
	}

	if(t->eof)
		return tok_eof;

	t->linep = skipspace(t->linep);

	while(!t->linep || !*t->linep){
		free(t->line);
		t->line = t->linep = read_line(t->f);

		if(!t->line){
			t->eof = 1;

			if(ferror(t->f))
				t->ferr = errno;

			/* file closed later */
			return tok_eof;
		}

		t->lno++;
		t->linep = skipspace(t->linep);
	}

	switch(*t->linep++){
#define OTHER(x)
#define KW(x)
#define PUNCT(t, c) case c: return tok_ ## t;
#define OP(x)
#define CMP(x)
		TOKENS
#undef OTHER
#undef KW
#undef PUNCT
#undef OP
#undef CMP

		case '#':
			for(; *t->linep && *t->linep != '\n'; t->linep++);
			return token_next(t);

		default:
			t->linep--;
	}

	if('0' <= *t->linep && *t->linep <= '9'){
		char *end;
		t->lastint = strtol(t->linep, &end, 0);
		assert(end > t->linep);
		t->linep = end;
		return tok_int;
	}

	for(i = 0; i < countof(keywords); i++)
		if(consume_word(t, keywords[i].kw))
			return keywords[i].tok;

	if(*t->linep == '$' && isident(t->linep[1], 0)){
		++t->linep;

		free(t->lastident);
		t->lastident = tokenise_ident(t);

		return tok_ident;
	}

	if(isident(*t->linep, 0)){
		free(t->lastbareword);
		t->lastbareword = tokenise_ident(t);
		return tok_bareword;
	}

	fprintf(stderr, "unknown token '%s'\n", t->linep);

	return tok_unknown;
}

enum token token_peek(tokeniser *t)
{
	if(t->unget == TOKEN_PEEK_EMPTY)
		t->unget = token_next(t);

	return t->unget;
}

bool token_accept(tokeniser *t, enum token tk)
{
	if(token_peek(t) == tk){
		token_next(t);
		return true;
	}
	return false;
}

void token_curline(tokeniser *t, char *out, size_t len)
{
	const char *src = t->line;
	const char *i, *anchor = NULL;
	size_t off;

	if(!src){
		snprintf(out, len, "[eof]");
		return;
	}

	off = t->linep - src;

	if(off > len / 2)
		src = t->linep - len / 2;

	/* ensure we're not before linep's line */
	for(i = src; i < t->linep; i++)
		if(*i == '\n')
			anchor = i + 1;

	if(anchor)
		src = anchor;

	for(; len > 1 && *src && *src != '\n'; src++, out++, len--)
		*out = *src;
	*out = '\0';
}

unsigned token_curlineno(tokeniser *t)
{
	return t->lno;
}

const char *token_curfile(tokeniser *t)
{
	return t->fname;
}

int token_last_int(tokeniser *t)
{
	return t->lastint;
}

static char *token_last_(char **p)
{
	char *ret = *p;
	*p = NULL;

	if(!ret)
		ret = xstrdup("?");

	return ret;
}

char *token_last_ident(tokeniser *t)
{
	return token_last_(&t->lastident);
}

char *token_last_bareword(tokeniser *t)
{
	return token_last_(&t->lastbareword);
}

int token_is_op(enum token t, enum op *const o)
{
	switch(t){
#define KW(t) case tok_ ## t: break;
#define OTHER KW
#define PUNCT(t, c) case tok_ ## t: break;
#define OP(t) case tok_ ## t: *o = op_ ## t; return 1;
#define CMP KW
	TOKENS
#undef CMP
#undef OP
#undef PUNCT
#undef OTHER
#undef KW
	}
	return 0;
}

int token_is_cmp(enum token t, enum op_cmp *const o)
{
	switch(t){
#define KW(t) case tok_ ## t: break;
#define OTHER KW
#define PUNCT(t, c) case tok_ ## t: break;
#define OP(t) case tok_ ## t: break;
#define CMP(t) case tok_ ## t: *o = cmp_ ## t; return 1;
	TOKENS
#undef CMP
#undef OP
#undef PUNCT
#undef OTHER
#undef KW
	}
	return 0;
}
