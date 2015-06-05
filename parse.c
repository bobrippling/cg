#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <assert.h>

#include "mem.h"

#include "block.h"
#include "tokenise.h"

#include "parse.h"

#include "dynmap.h"
#include "val.h"
#include "val_allocas.h"
#include "isn.h"

typedef struct {
	tokeniser *tok;
	unit *unit;
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

	fprintf(stderr, "%s:%u: ", token_curfile(p->tok), token_curlineno(p->tok));

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
	global *glob;
	variable *var;
	size_t arg_idx;

	if(p->names2vals){
		v = dynmap_get(char *, val *, p->names2vals, name);
		if(v){
found:
			if(opts & VAL_CREATE)
				parse_error(p, "pre-existing identifier '%s'", name);

			free(name);

			return v;
		}
	}

	/* check args */
	if((var = function_arg_find(p->func, name, &arg_idx))){
		v = val_new_arg(arg_idx, name, variable_size(var, 0));

		map_val(p, name, v);

		name = NULL;

		goto found;
	}

	/* check globals */
	glob = unit_global_find(p->unit, name);

	if(glob){
		v = val_new_lbl(name);
		name = NULL;
		goto found;
	}

	if((opts & VAL_CREATE) == 0)
		parse_error(p, "undeclared identifier '%s'", name);

	if(opts & VAL_ALLOCA){
		v = val_alloca();
	}else{
		v = val_name_new(size, name);
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

static int parse_finished(tokeniser *tok)
{
	return token_peek(tok) == tok_eof || token_peek(tok) == tok_unknown;
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

static void parse_call(parse *p, char *ident_or_null)
{
	val *target;
	val *into;
	unsigned sz = parse_dot_size(p);
	dynarray args = DYNARRAY_INIT;

	assert(ident_or_null && "TODO: void");

	target = parse_rval(p, 0);

	into = uniq_val(p, ident_or_null, sz, VAL_CREATE);

	eat(p, "call paren", tok_lparen);

	while(1){
		val *arg;

		if(dynarray_is_empty(&args)){
			if(token_peek(p->tok) == tok_rparen)
				break; /* call x() */
		}else{
			eat(p, "call comma", tok_comma);
		}

		arg = parse_rval(p, 0);

		dynarray_add(&args, arg);

		if(token_peek(p->tok) == tok_rparen || parse_finished(p->tok))
			break;
	}

	eat(p, "call paren", tok_rparen);

	isn_call(p->entry, into, target, &args);

	dynarray_reset(&args);
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
			char *ident = lhs ? xstrdup(lhs) : NULL;
			val *index_into = parse_lval(p);
			val *idx;

			eat(p, "elem", tok_comma);

			idx = parse_rval(p, 0);

			/*vlhs = val_element(NULL, index_into, idx, 0, ident);*/
			vlhs = val_name_new(0, ident);

			map_val(p, lhs, vlhs);

			isn_elem(p->entry, index_into, idx, vlhs);
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

		case tok_call:
		{
			parse_call(p, lhs);
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

static void enter_unreachable_code(parse *p)
{
	p->entry = NULL;
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

	isn_br(p->entry, cond, btrue, bfalse);

	enter_unreachable_code(p);
}

static void parse_jmp(parse *p)
{
	block *target;
	char *lbl;

	eat(p, "jmp label", tok_ident);
	lbl = token_last_ident(p->tok);

	target = function_block_find(p->func, lbl, NULL);

	isn_jmp(p->entry, target);

	enter_unreachable_code(p);
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

		case tok_call:
			parse_call(p, NULL);
			break;

		case tok_ident:
		{
			char *ident = token_last_ident(p->tok);

			if(token_peek(p->tok) == tok_colon){
				int created;
				block *from = p->entry;

				eat(p, "label colon", tok_colon);

				p->entry = function_block_find(p->func, xstrdup(ident), &created);

				if(!created && !block_tenative(p->entry)){
					parse_error(p, "block '%s' already exists", ident);

					/* use an anonymous block to prevent assertion failures
					 * in the backend */
					enter_unreachable_code(p);
				}

				free(ident), ident = NULL;

				if(p->entry && block_unknown_ending(from)){
					/* current block is fall-thru */
					isn_jmp(from, p->entry);
				}
			}else{
				parse_ident(p);
			}
			break;
		}

		case tok_jmp:
			parse_jmp(p);
			break;

		case tok_br:
			parse_br(p);
			break;

		case tok_store:
			parse_store(p);
			break;
	}
}

static void parse_decl_start(parse *p, unsigned *const sz, char **const name)
{
	eat(p, "decl type", tok_int);
	*sz = token_last_int(p->tok);

	eat(p, "decl name", tok_ident);
	*name = token_last_ident(p->tok);

	if(!*name)
		*name = xstrdup("_error");
}

static function *parse_function(parse *p, unsigned ret, char *name)
{
	function *fn = unit_function_new(p->unit, name, ret);

	eat(p, "function open paren", tok_lparen);

	while(token_peek(p->tok) != tok_rparen && !parse_finished(p->tok)){
		char *arg_name;
		unsigned arg_sz;
		parse_decl_start(p, &arg_sz, &arg_name);

		function_arg_add(fn, arg_sz, arg_name);

		if(token_peek(p->tok) != tok_comma)
			break;

		eat(p, "arg comma", tok_comma);
	}

	eat(p, "function close paren", tok_rparen);

	if(token_peek(p->tok) == tok_semi){
		/* declaration */
		eat(p, "function semi", tok_semi);
		return fn;
	}

	eat(p, "function open brace", tok_lbrace);

	p->func = fn;
	p->entry = function_entry_block(fn, true);

	while(token_peek(p->tok) != tok_rbrace && !parse_finished(p->tok)){
		parse_block(p);
	}

	eat(p, "function close brace", tok_rbrace);

	function_finalize(fn);

	return fn;
}

static void parse_variable(parse *p, unsigned sz, char *name)
{
	eat(p, "variable end", tok_semi);

	unit_variable_new(p->unit, name, sz);
}

static void parse_global(parse *p)
{
	unsigned sz;
	char *name;

	parse_decl_start(p, &sz, &name);

	if(token_peek(p->tok) == tok_lparen)
		parse_function(p, sz, name);
	else
		parse_variable(p, sz, name);

	dynmap_clear(p->names2vals);
}

unit *parse_code(tokeniser *tok, int *const err)
{
	parse state = { 0 };

	state.tok = tok;
	state.unit = unit_new();

	while(!parse_finished(tok)){
		parse_global(&state);
	}

	if(state.entry && block_unknown_ending(state.entry)){
		parse_error(&state, "control reaches end of function");
	}

	/* char* => val*
	 * the char* is present in the name-value and owned by it */
	dynmap_free(state.names2vals);

	*err = state.err;

	return state.unit;
}
