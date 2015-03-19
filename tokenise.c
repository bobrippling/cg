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

struct tokeniser
{
	FILE *f;
	int eof;
	int ferr;

	char *line, *linep;

	int free_lastident;
	char *lastident;
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
	TOKENS
#undef PUNCT
#undef OTHER
#undef KW
};

tokeniser *token_init(FILE *f)
{
	tokeniser *t = xmalloc(sizeof *t);
	memset(t, 0, sizeof *t);

	t->f = f;

	return t;
}

static void free_lastident(tokeniser *t)
{
	if(t->free_lastident)
		free(t->lastident);
}

void token_fin(tokeniser *t, int *const err)
{
	*err = t->ferr;

	if(t->f)
		fclose(t->f);

	free_lastident(t);
	free(t);
}

const char *token_to_str(enum token t)
{
	switch(t){
#define OTHER(x) case tok_ ## x: return #x;
#define KW(x) case tok_ ## x: return "tok_" #x;
#define PUNCT(x, p) case tok_ ## x: return #p;
		TOKENS
#undef OTHER
#undef KW
#undef PUNCT
	}
	assert(0);
}

static int consume_word(tokeniser *t, const char *word)
{
	if(!str_beginswith(t->linep, word))
		return 0;

	t->linep += strlen(word);
	return 1;
}

enum token token_next(tokeniser *t)
{
	size_t i;

	if(t->linep)
		for(; isspace(*t->linep); t->linep++);

	if(!t->linep || !*t->linep){
		if(t->eof)
			return tok_eof;

		free(t->line);
		t->line = t->linep = read_line(t->f);

		if(!t->line){
			t->eof = 1;

			if(ferror(t->f))
				t->ferr = errno;

			fclose(t->f);
			return tok_eof;
		}
	}

	switch(*t->linep++){
		case '(': return tok_lparen;
		case ')': return tok_rparen;
		case '.': return tok_dot;
		case ',': return tok_comma;
		case '=': return tok_equal;

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

	if(isalpha(*t->linep)){
		char *end;
		char *buf;
		size_t len;

		for(end = t->linep + 1; isalnum(*end); end++);

		len = end - t->linep + 1;
		buf = xmalloc(len);
		memcpy(buf, t->linep, len - 1);
		buf[len - 1] = '\0';

		free_lastident(t);
		t->lastident = buf;
		t->free_lastident = 1;

		t->linep += len - 1;

		return tok_ident;
	}

	fprintf(stderr, "unknown token '%s'\n", t->linep);

	return tok_unknown;
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

int token_last_int(tokeniser *t)
{
	return t->lastint;
}

char *token_last_ident(tokeniser *t)
{
	t->free_lastident = 0;
	return t->lastident;
}