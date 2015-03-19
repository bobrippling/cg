#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include "block.h"
#include "tokenise.h"

#include "parse.h"

#include "dynmap.h"
#include "val.h"
#include "isn.h"

typedef struct {
	tokeniser *tok;
	block *entry;
	dynmap *names2vals;
} parse;

enum val_opts
{
	VAL_LVAL = 1 << 0,
	VAL_CREATE = 1 << 1
};

attr_printf(2, 3)
static void parse_error(parse *p, const char *fmt, ...)
{
	char buf[32];
	va_list l;

	va_start(l, fmt);
	vfprintf(stderr, fmt, l);
	va_end(l);
	fputc('\n', stderr);

	token_curline(p->tok, buf, sizeof buf);
	fprintf(stderr, "at: '%s'\n", buf);
}

static val *uniq_val(
		parse *p, const char *name, enum val_opts opts)
{
	val *v, *old;

	if(p->names2vals){
		v = dynmap_get(const char *, val *, p->names2vals, name);
		if(v){
			if(opts & VAL_CREATE)
				parse_error(p, "pre-existing identifier '%s'", name);
			return v;
		}

	}else{
		p->names2vals = dynmap_new(
				const char *, (dynmap_cmp_f *)strcmp, dynmap_strhash);
	}

	if((opts & VAL_CREATE) == 0)
		parse_error(p, "undeclared identifier '%s'", name);

	v = (opts & VAL_LVAL ? val_name_new_lval : val_name_new)();

	old = dynmap_set(const char *, val *, p->names2vals, name, v);
	assert(!old);

	return v;
}

static void eat(parse *p, const char *desc, enum token expect)
{
	enum token got = token_next(p->tok);
	if(got == expect)
		return;

	parse_error(p, "expected %s for %s, got %s",
			token_to_str(expect), desc, token_to_str(got));
}

static val *parse_lval(parse *p)
{
	enum token t = token_next(p->tok);

	switch(t){
		case tok_int:
			return val_new_ptr_from_int(token_last_int(p->tok));

		case tok_ident:
			return uniq_val(p, token_last_ident(p->tok), VAL_LVAL);

		default:
			parse_error(p, "memory operand expected, got %s", token_to_str(t));
			return val_new_ptr_from_int(0);
	}
}

static void parse_ident(parse *p)
{
	/* x = load y */
	const char *to = token_last_ident(p->tok);
	val *vto = uniq_val(p, to, VAL_CREATE | VAL_LVAL);

	eat(p, "assignment", tok_equal);

	switch(token_next(p->tok)){
		case tok_load:
			isn_load(p->entry, vto, parse_lval(p));
			break;

		case tok_alloca:
			eat(p, "alloca", tok_int);
			isn_alloca(p->entry, token_last_int(p->tok), vto);
			break;

		default:
			parse_error(p, "expected load or alloca");
	}
}

static void parse_ret(parse *p)
{
	eat(p, "ret", tok_ident);

	isn_ret(p->entry,
			uniq_val(
				p,
				token_last_ident(p->tok),
				0));
}

void parse_code(tokeniser *tok, block *entry)
{
	parse state = { 0 };

	state.tok = tok;
	state.entry = entry;

	for(;;){
		enum token ct = token_next(tok);

		switch(ct){
			default:
				assert(0 && "TODO");
			case tok_eof:
				goto fin;

			case tok_ret:
				parse_ret(&state);
				break;

			case tok_ident:
				parse_ident(&state);
				break;
		}
	}

fin:;
}
