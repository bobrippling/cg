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

	if(!t->linep || !*t->linep){
		free(t->line);
		t->line = t->linep = read_line(t->f);

		if(!t->line){
			if(ferror(t->f))
				t->ferr = errno;

			fclose(t->f);
		}
	}

	for(; isspace(*t->linep); t->linep++);

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

		for(end = t->linep + 1; isalpha(*end); end++);

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

	return tok_unknown;
}

int token_last_int(tokeniser *t)
{
	return t->lastint;
}

const char *token_last_ident(tokeniser *t)
{
	t->free_lastident = 0;
	return t->lastident;
}
