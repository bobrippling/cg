#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include "block.h"
#include "tokenise.h"

#include "parse.h"

#include "dynmap.h"
#include "val.h"
#include "val_allocas.h"
#include "isn.h"

typedef struct {
	tokeniser *tok;
	block *entry;
	dynmap *names2vals;
	int err;
} parse;

enum val_opts
{
	VAL_LVAL = 1 << 0,
	VAL_CREATE = 1 << 1,
	VAL_ALLOCA = 1 << 2
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

	p->err = 1;
}

static void create_names2vals(parse *p)
{
	if(p->names2vals)
		return;

	p->names2vals = dynmap_new(
			const char *, (dynmap_cmp_f *)strcmp, dynmap_strhash);
}

static val *map_val(parse *p, const char *name, val *v)
{
	val *old;

	create_names2vals(p);

	old = dynmap_set(const char *, val *, p->names2vals, name, v);
	assert(!old);

	return v;
}

static val *uniq_val(
		parse *p, const char *name, enum val_opts opts)
{
	val *v;

	if(p->names2vals){
		v = dynmap_get(const char *, val *, p->names2vals, name);
		if(v){
			if(opts & VAL_CREATE)
				parse_error(p, "pre-existing identifier '%s'", name);
			return v;
		}
	}

	if((opts & VAL_CREATE) == 0)
		parse_error(p, "undeclared identifier '%s'", name);

	if(opts & VAL_ALLOCA){
		assert(opts & VAL_LVAL);

		v = val_alloca();
	}else{
		v = (opts & VAL_LVAL ? val_name_new_lval : val_name_new)();
	}

	return map_val(p, name, v);
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
			return uniq_val(p, token_last_ident(p->tok), 0);

		default:
			parse_error(p, "memory operand expected, got %s", token_to_str(t));
			return val_new_ptr_from_int(0);
	}
}

static val *parse_rval(parse *p)
{
	enum token t = token_next(p->tok);

	switch(t){
		case tok_int:
			return val_new_i(token_last_int(p->tok));

		case tok_ident:
			return uniq_val(p, token_last_ident(p->tok), 0);

		default:
			parse_error(p, "rvalue operand expected, got %s", token_to_str(t));
			return val_new_i(0);
	}
}

static void parse_ident(parse *p)
{
	/* x = load y */
	const char *lhs = token_last_ident(p->tok);

	eat(p, "assignment", tok_equal);

	switch(token_next(p->tok)){
		case tok_load:
		{
			val *vlhs = uniq_val(p, lhs, VAL_CREATE | VAL_LVAL);
			isn_load(p->entry, vlhs, parse_lval(p));
			break;
		}

		case tok_alloca:
		{
			unsigned amt;
			val *vlhs;

			eat(p, "alloca", tok_int);
			amt = token_last_int(p->tok);

			vlhs = uniq_val(p, lhs, VAL_CREATE | VAL_LVAL | VAL_ALLOCA);

			isn_alloca(p->entry, amt, vlhs);
			break;
		}

		case tok_elem:
		{
			val *vlhs;
			val *index_into = parse_lval(p);
			unsigned idx;

			eat(p, "elem", tok_comma);
			eat(p, "elem", tok_int);

			idx = token_last_int(p->tok);
			vlhs = val_element(NULL, index_into, idx, 1);

			map_val(p, lhs, vlhs);

			isn_elem(p->entry, index_into, val_new_i(idx), vlhs);
			break;
		}

		default:
			parse_error(p, "expected load, alloca or elem");
	}
}

static void parse_ret(parse *p)
{
	isn_ret(p->entry, parse_rval(p));
}

static void parse_store(parse *p)
{
	val *lval = parse_lval(p);
	val *rval;

	eat(p, "store", tok_comma);

	rval = parse_rval(p);

	isn_store(p->entry, rval, lval);
}

void parse_code(tokeniser *tok, block *entry, int *const err)
{
	parse state = { 0 };

	state.tok = tok;
	state.entry = entry;

	for(;;){
		enum token ct = token_next(tok);

		switch(ct){
			default:
				parse_error(&state, "unexpected token %s", token_to_str(ct));
				goto fin;

			case tok_eof:
				goto fin;

			case tok_ret:
				parse_ret(&state);
				break;

			case tok_ident:
				parse_ident(&state);
				break;

			case tok_store:
				parse_store(&state);
				break;
		}
	}

fin:
	*err = state.err;
}
