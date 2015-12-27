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
#include "isn.h"
#include "type.h"
#include "variable_struct.h"

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

static type *parse_type(parse *);


attr_printf(2, 0)
static void error_v(parse *p, const char *fmt, va_list l)
{
	char buf[32];

	fprintf(stderr, "%s:%u: ", token_curfile(p->tok), token_curlineno(p->tok));

	vfprintf(stderr, fmt, l);
	fputc('\n', stderr);

	token_curline(p->tok, buf, sizeof buf);
	fprintf(stderr, "at: '%s'\n", buf);

	p->err = 1;
}

attr_printf(2, 3)
static void parse_error(parse *p, const char *fmt, ...)
{
	va_list l;

	va_start(l, fmt);
	error_v(p, fmt, l);
	va_end(l);
}

attr_printf(2, 3)
static void sema_error(parse *p, const char *fmt, ...)
{
	va_list l;

	va_start(l, fmt);
	error_v(p, fmt, l);
	va_end(l);
}

static type *default_type(parse *p)
{
	return type_get_primitive(unit_uniqtypes(p->unit), i4);
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
		parse *p,
		char *name,
		type *ty,
		enum val_opts opts)
{
	val *v;
	global *glob;
	variable *var;
	size_t arg_idx;

	if(ty){
		assert(opts & VAL_CREATE);
	}else{
		assert(!(opts & VAL_CREATE));
	}

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
		v = val_new_argument(var);

		map_val(p, name, v);

		name = NULL;

		goto found;
	}

	/* check globals */
	glob = unit_global_find(p->unit, name);

	if(glob){
		v = val_new_global(glob);
		name = NULL;
		goto found;
	}

	if((opts & VAL_CREATE) == 0)
		parse_error(p, "undeclared identifier '%s'", name);

	if(opts & VAL_ALLOCA){
		v = val_new_local(NULL);
	}else{
#if 0
		var = (variable){ }; /* TODO */
#endif
		(void)ty;
		v = val_new_local(var);
	}

	return map_val(p, name, v);
}

static void eat(parse *p, const char *desc, enum token expect)
{
	enum token got = token_next(p->tok);
	if(got == expect)
		return;

	parse_error(p, "expected %s%s%s, got %s",
			token_to_str(expect),
			desc ? " for " : "",
			desc,
			token_to_str(got));
}

static int parse_finished(tokeniser *tok)
{
	return token_peek(tok) == tok_eof || token_peek(tok) == tok_unknown;
}

static void parse_type_list(
		parse *p, dynarray *types,
		dynarray *toplvl_args,
		enum token lasttok)
{
	if(token_peek(p->tok) != lasttok){
		for(;;){
			type *memb = parse_type(p);

			dynarray_add(types, memb);

			if(toplvl_args){
				char *ident;

				eat(p, "argument name", tok_ident);

				ident = token_last_ident(p->tok);
				dynarray_add(toplvl_args, ident);
			}

			if(token_accept(p->tok, tok_comma))
				continue;
			break;
		}
	}

	eat(p, NULL, lasttok);
}


static type *parse_type_maybe_func(parse *p, dynarray *toplvl_args)
{
	/*
	 * void
	 * i1, i2, i4, i8
	 * f4, f8, flarge
	 * { i2, f4 }
	 * [ i2 x 7 ]
	 * i8 *
	 * f4 (i2)
	 */
	type *t = NULL;
	enum token tok;

	switch((tok = token_next(p->tok))){
			enum type_primitive prim;

		case tok_ident:
			/* TODO: type alias lookup */
			parse_error(p, "TODO: type alias");
			return default_type(p);

		case tok_i1: prim = i1; goto prim;
		case tok_i2: prim = i2; goto prim;
		case tok_i4: prim = i4; goto prim;
		case tok_i8: prim = i8; goto prim;

		case tok_f4:     prim = f4; goto prim;
		case tok_f8:     prim = f8; goto prim;
		case tok_flarge: prim = flarge; goto prim;
prim:
			t = type_get_primitive(unit_uniqtypes(p->unit), prim);
			break;

		case tok_void:
			t = type_get_void(unit_uniqtypes(p->unit));
			break;

		case tok_lbrace:
		{
			dynarray types = DYNARRAY_INIT;

			parse_type_list(p, &types, NULL, tok_rbrace);

			t = type_get_struct(unit_uniqtypes(p->unit), &types);
			break;
		}

		case tok_lsquare:
		{
			type *elemty = parse_type(p);
			char *mul;
			unsigned nelems;

			eat(p, "array multiplier", tok_ident);
			mul = token_last_ident(p->tok);
			if(strcmp(mul, "x")){
				parse_error(p, "'x' expected for array multiplier, got %s", mul);
			}
			free(mul);

			eat(p, "array multiple", tok_int);
			nelems = token_last_int(p->tok);

			t = type_get_array(unit_uniqtypes(p->unit), elemty, nelems);
			break;
		}

		default:
			parse_error(p, "type expected, got %s", token_to_str(tok));
			return default_type(p);
	}

	for(;;){
		if(token_accept(p->tok, tok_star)){
			t = type_get_ptr(unit_uniqtypes(p->unit), t);
			continue;
		}

		if(token_accept(p->tok, tok_lparen)){
			dynarray types = DYNARRAY_INIT;

			parse_type_list(p, &types, toplvl_args, tok_rparen);

			t = type_get_func(unit_uniqtypes(p->unit), t, &types);
			continue;
		}

		break;
	}

	return t;
}

static type *parse_type(parse *p)
{
	return parse_type_maybe_func(p, NULL);
}

static val *parse_val(parse *p)
{
	type *ty;

	if(token_peek(p->tok) == tok_ident){
		char *ident = token_last_ident(p->tok);
		val *v = uniq_val(p, ident, NULL, 0);

		return v;
	}

	/* need a type and a literal, e.g. i32 5 */
	ty = parse_type(p);

	if(!ty){
		parse_error(p, "value type expected, got %s",
				token_to_str(token_peek(p->tok)));

		ty = default_type(p);
	}

	if(token_accept(p->tok, tok_int)){
		int i = token_last_int(p->tok);

		return val_new_i(i, ty);
	}

	parse_error(p, "value operand expected, got %s",
			token_to_str(token_peek(p->tok)));

	return val_new_i(0, ty);
}

static void parse_call(parse *p, char *ident_or_null)
{
	val *target;
	val *into;
	dynarray args = DYNARRAY_INIT;
	type *retty;

	assert(ident_or_null && "TODO: void");

	target = parse_val(p);

	if(!(retty = type_func_call(val_type(target)))){
		retty = default_type(p);
		sema_error(p, "call requires function operand");
	}

	into = uniq_val(p, ident_or_null, retty, VAL_CREATE);

	eat(p, "call paren", tok_lparen);

	while(1){
		val *arg;

		if(dynarray_is_empty(&args)){
			if(token_peek(p->tok) == tok_rparen)
				break; /* call x() */
		}else{
			eat(p, "call comma", tok_comma);
		}

		arg = parse_val(p);

		dynarray_add(&args, arg);

		if(token_peek(p->tok) == tok_rparen || parse_finished(p->tok))
			break;

#warning tycheck
	}

	eat(p, "call paren", tok_rparen);

	isn_call(p->entry, into, target, &args);

	dynarray_reset(&args);
}

static void parse_ident(parse *p)
{
	char *spel = token_last_ident(p->tok);
	enum token tok;

	eat(p, "assignment", tok_equal);

	tok = token_next(p->tok);

	switch(tok){
		case tok_load:
		{
			val *rhs = parse_val(p);
			type *deref_ty = type_deref(val_type(rhs));
			val *lhs;

			if(!deref_ty){
				sema_error(p, "load operand not a pointer type");
				deref_ty = default_type(p);
			}

			lhs = uniq_val(p, spel, deref_ty, VAL_CREATE);
			isn_load(p->entry, lhs, rhs);
			break;
		}

		case tok_alloca:
		{
			val *vlhs;
			type *ty = parse_type(p);

			vlhs = uniq_val(
					p, spel,
					type_get_ptr(unit_uniqtypes(p->unit), ty),
					VAL_CREATE | VAL_ALLOCA);

			isn_alloca(p->entry, vlhs);
			break;
		}

		case tok_elem:
		{
#if 0
			val *vlhs;
			char *ident = spel ? xstrdup(spel) : NULL;
			val *index_into = parse_val(p);
			val *idx;

			eat(p, "elem", tok_comma);

			idx = parse_val(p);

			vlhs = val_new_local(ident);

			map_val(p, spel, vlhs);

			isn_elem(p->entry, index_into, idx, vlhs);
			break;
#endif
			assert(0 && "TODO: elem");
			break;
		}

		case tok_zext:
		{
			unsigned to;
			val *from;
			val *vres;
			type *ty_to;
			enum type_primitive prim;

			eat(p, "extend-to", tok_int);
			to = token_last_int(p->tok);

			eat(p, "zext", tok_comma);

			from = parse_val(p);

			if(!type_size_to_primitive(to, &prim)){
				sema_error(p, "zext operand not a valid integer size");
				prim = i4;
			}

			ty_to = type_get_primitive(unit_uniqtypes(p->unit), prim);

			vres = uniq_val(p, spel, ty_to, VAL_CREATE);

#warning tycheck / ensure from is int

			isn_zext(p->entry, from, vres);
			break;
		}

		case tok_call:
		{
			parse_call(p, spel);
			break;
		}

		default:
		{
			int is_cmp = 0;
			enum op op;
			enum op_cmp cmp;

			if(token_is_op(tok, &op) || (is_cmp = 1, token_is_cmp(tok, &cmp))){
				/* x = add a, b */
				val *vlhs = parse_val(p);
				val *vrhs = (eat(p, "operator", tok_comma), parse_val(p));
				val *vres;
				type *opty;

				if(val_type(vlhs) != val_type(vrhs)){
					sema_error(p, "mismatching types in op");
				}

				opty = val_type(vlhs);
				vres = uniq_val(p, spel, opty, VAL_CREATE);

#warning tycheck / pointer-vs-int check

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
#warning tycheck
	isn_ret(p->entry, parse_val(p));
}

static void parse_store(parse *p)
{
	val *lval;
	val *rval;

	lval = parse_val(p);
	eat(p, "store comma", tok_comma);
	rval = parse_val(p);

#warning tycheck

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
	val *cond = parse_val(p);

#warning tycheck cond must be integral

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

		case tok_br:
			parse_br(p);
			break;

		case tok_store:
			parse_store(p);
			break;
	}
}

static void parse_function(
		parse *p,
		char *name, type *ty,
		dynarray *toplvl_args)
{
	function *fn = unit_function_new(p->unit, name, ty, toplvl_args);

	if(token_accept(p->tok, tok_semi)){
		/* declaration */
		return;
	}

	eat(p, "function open brace", tok_lbrace);

	p->func = fn;
	p->entry = function_entry_block(fn, true);

	while(token_peek(p->tok) != tok_rbrace && !parse_finished(p->tok)){
		parse_block(p);
	}

	dynmap_clear(p->names2vals);

	eat(p, "function close brace", tok_rbrace);

	function_finalize(fn);
}

static void parse_variable(parse *p, char *name, type *ty)
{
	/* TODO: init */
	unit_variable_new(p->unit, name, ty);
}

static void parse_global(parse *p)
{
	type *ty;
	char *name;
	dynarray toplvl_args = DYNARRAY_INIT;

	eat(p, "decl name", tok_ident);
	name = token_last_ident(p->tok);
	if(!name)
		name = xstrdup("_error");

	ty = parse_type_maybe_func(p, &toplvl_args);

	if(type_is_fn(ty)){
		parse_function(p, name, ty, &toplvl_args);
	}else{
		parse_variable(p, name, ty);
	}
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
