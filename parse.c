#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <assert.h>

#include "block.h"
#include "branch.h"
#include "tokenise.h"

#include "parse.h"

#include "dynmap.h"
#include "val.h"
#include "val_allocas.h"
#include "isn.h"

typedef struct {
	tokeniser *tok;
	function *func;
	block *entry;
	dynmap *names2vals;
	int err;
} parse;

enum val_opts
{
	VAL_CREATE = 1 << 0,
	VAL_ALLOCA = 1 << 1
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

static val *map_val(parse *p, char *name, val *v)
{
	val *old;

	create_names2vals(p);

	old = dynmap_set(char *, val *, p->names2vals, name, v);
	assert(!old);

	return v;
}

static val *uniq_val(
		parse *p, char *name, int size, enum val_opts opts)
{
	val *v;

	if(p->names2vals){
		v = dynmap_get(char *, val *, p->names2vals, name);
		if(v){
			if(opts & VAL_CREATE)
				parse_error(p, "pre-existing identifier '%s'", name);

			free(name);

			return v;
		}
	}

	if((opts & VAL_CREATE) == 0)
		parse_error(p, "undeclared identifier '%s'", name);

	if(opts & VAL_ALLOCA){
		v = val_alloca();
	}else{
		assert(size >= 0);
		v = val_name_new(size);
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
			return uniq_val(p, token_last_ident(p->tok), -1, 0);

		default:
			parse_error(p, "memory operand expected, got %s", token_to_str(t));
			return val_new_ptr_from_int(0);
	}
}

static int parse_dot_size(parse *p)
{
	eat(p, "dot-size", tok_dot);
	eat(p, "dot-size", tok_int);
	return token_last_int(p->tok);
}

static val *parse_rval(parse *p, unsigned size)
{
	enum token t = token_next(p->tok);

	switch(t){
		case tok_int:
		{
			int i = token_last_int(p->tok);
			return val_new_i(i, size);
		}

		case tok_ident:
		{
			char *ident = token_last_ident(p->tok);
			return uniq_val(p, ident, size, 0);
		}

		default:
			parse_error(p, "rvalue operand expected, got %s", token_to_str(t));
			return val_new_i(0, size);
	}
}

static void parse_ident(parse *p)
{
	/* x = load y */
	char *lhs = token_last_ident(p->tok);
	enum token tok;

	eat(p, "assignment", tok_equal);

	tok = token_next(p->tok);

	switch(tok){
		case tok_load:
		{
			unsigned isn_sz = parse_dot_size(p);
			val *vlhs = uniq_val(p, lhs, isn_sz, VAL_CREATE);
			isn_load(p->entry, vlhs, parse_lval(p));
			break;
		}

		case tok_alloca:
		{
			unsigned amt;
			val *vlhs;

			eat(p, "alloca", tok_int);
			amt = token_last_int(p->tok);

			vlhs = uniq_val(p, lhs, -1, VAL_CREATE | VAL_ALLOCA);

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
			vlhs = val_element(NULL, index_into, idx, 0);

			map_val(p, lhs, vlhs);

			isn_elem(p->entry, index_into, val_new_i(idx, 0), vlhs);
			break;
		}

		case tok_zext:
		{
			unsigned to;
			val *from;
			val *vres;

			eat(p, "extend-to", tok_int);
			to = token_last_int(p->tok);

			eat(p, "zext", tok_comma);

			from = parse_rval(p, /*unused except for literal int*/to);

			vres = uniq_val(p, lhs, to, VAL_CREATE);
			isn_zext(p->entry, from, vres);
			break;
		}

		default:
		{
			int is_cmp = 0;
			enum op op;
			enum op_cmp cmp;

			if(token_is_op(tok, &op) || (is_cmp = 1, token_is_cmp(tok, &cmp))){
				/* x = add a, b */
				unsigned isn_sz = parse_dot_size(p);
				val *vlhs = parse_rval(p, isn_sz);
				val *vrhs = (eat(p, "operator", tok_comma), parse_rval(p, isn_sz));
				val *vres = uniq_val(p, lhs, is_cmp ? 1 : isn_sz, VAL_CREATE);

				if(is_cmp)
					isn_cmp(p->entry, cmp, vlhs, vrhs, vres);
				else
					isn_op(p->entry, op, vlhs, vrhs, vres);

			}else{
				parse_error(p, "expected load, alloca, elem or operator");
			}
			break;
		}
	}
}

static void parse_ret(parse *p)
{
	isn_ret(p->entry, parse_rval(p, parse_dot_size(p)));
}

static void parse_store(parse *p)
{
	val *lval;
	val *rval;
	unsigned isn_sz;

	isn_sz = parse_dot_size(p);

	lval = parse_lval(p);
	eat(p, "store", tok_comma);
	rval = parse_rval(p, isn_sz);

	isn_store(p->entry, rval, lval);
}

static void parse_br(parse *p)
{
	/* br cond, ltrue, lfalse */
	char *ltrue, *lfalse;
	block *btrue, *bfalse;
	val *cond = parse_rval(p, 1);

	eat(p, "br comma", tok_comma);
	eat(p, "br true", tok_ident);
	ltrue = token_last_ident(p->tok);

	eat(p, "br comma", tok_comma);
	eat(p, "br false", tok_ident);
	lfalse = token_last_ident(p->tok);

	btrue = function_block_find(p->func, ltrue, NULL);
	bfalse = function_block_find(p->func, lfalse, NULL);

	branch_cond(cond, p->entry, btrue, bfalse);
}

static void parse_block(parse *p)
{
	enum token ct = token_next(p->tok);

	switch(ct){
		default:
			parse_error(p, "unexpected token %s", token_to_str(ct));
			break;

		case tok_eof:
			break;

		case tok_ret:
			parse_ret(p);
			break;

		case tok_ident:
		{
			char *ident = token_last_ident(p->tok);

			if(token_peek(p->tok) == tok_colon){
				int created;

				eat(p, "label colon", tok_colon);

				p->entry = function_block_find(p->func, ident, &created);

				if(!created && !block_tenative(p->entry))
					parse_error(p, "block '%s' already exists", ident);

			}else{
				parse_ident(p);
			}
			break;
		}

		case tok_br:
			parse_br(p);
			break;

		case tok_store:
			parse_store(p);
			break;
	}
}

static int parse_finished(tokeniser *tok)
{
	return token_peek(tok) == tok_eof || token_peek(tok) == tok_unknown;
}

static function *parse_function(parse *p)
{
	unsigned ret;
	char *name;
	function *fn;

	eat(p, "function return", tok_int);
	ret = token_last_int(p->tok);

	eat(p, "function name", tok_ident);
	name = token_last_ident(p->tok);

	eat(p, "function open paren", tok_lparen);
	/* TODO: arguments */
	eat(p, "function close paren", tok_rparen);

	eat(p, "function open brace", tok_lbrace);

	fn = function_new(name, ret);
	p->func = fn;
	p->entry = function_entry_block(fn);

	while(token_peek(p->tok) != tok_rbrace && !parse_finished(p->tok)){
		parse_block(p);
	}

	eat(p, "function close brace", tok_rbrace);

	return fn;
}

static void free_names2vals(dynmap *names2vals)
{
	size_t i;
	char *name;

	for(i = 0; (name = dynmap_key(char *, names2vals, i)); i++)
		free(name);

	dynmap_free(names2vals);
}

unit *parse_code(tokeniser *tok, int *const err)
{
	parse state = { 0 };
	unit *unit = unit_new();

	state.tok = tok;

	while(!parse_finished(tok)){
		function *f = parse_function(&state);

		if(f)
			unit_add_function(unit, f);
	}

	free_names2vals(state.names2vals);

	*err = state.err;

	return unit;
}
