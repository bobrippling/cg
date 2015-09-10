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
	size_t off;

	fprintf(stderr, "%s:%u: ", token_curfile(p->tok), token_curlineno(p->tok));

	vfprintf(stderr, fmt, l);
	fputc('\n', stderr);

	token_curline(p->tok, buf, sizeof buf, &off);
	fprintf(stderr, "at: '%s'\n", buf);
	fprintf(stderr, "     ");

	fprintf(stderr, "%*c^\n", (int)off, ' ');

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
	type *arg_ty;
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
	if(function_arg_find(p->func, name, &arg_idx, &arg_ty)){
		v = val_new_argument(name, arg_idx, arg_ty, p->func);

		map_val(p, name, v);

		name = NULL;

		goto found;
	}

	/* check globals */
	glob = unit_global_find(p->unit, name);

	if(glob){
		v = val_new_global(unit_uniqtypes(p->unit), glob);
		name = NULL;
		goto found;
	}

	if((opts & VAL_CREATE) == 0)
		parse_error(p, "undeclared identifier '%s'", name);

	v = val_new_local(name, ty, opts & VAL_ALLOCA);

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
		bool have_idents = false;

		for(;;){
			type *memb = parse_type(p);

			if(type_is_fn(memb)){
				sema_error(p, "function in aggregate");
				memb = default_type(p);
			}

			if(toplvl_args){
				if(dynarray_is_empty(types)){
					/* first time, decide whether to have arg names */
					if(token_peek(p->tok) == tok_ident){
						have_idents = true;
					}else{
						have_idents = false;
					}
				}

				if(have_idents){
					char *ident;

					eat(p, "argument name", tok_ident);

					ident = token_last_ident(p->tok);
					dynarray_add(toplvl_args, ident);
				}
			}

			dynarray_add(types, memb);

			if(token_accept(p->tok, tok_comma))
				continue;
			break;
		}
	}

	eat(p, NULL, lasttok);
}


static type *parse_type_maybe_func_nochk(parse *p, dynarray *toplvl_args)
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

			eat(p, "array multiplier", tok_bareword);
			mul = token_last_bareword(p->tok);
			if(strcmp(mul, "x")){
				parse_error(p, "'x' expected for array multiplier, got %s", mul);
			}
			free(mul);

			eat(p, "array multiple", tok_int);
			nelems = token_last_int(p->tok);

			t = type_get_array(unit_uniqtypes(p->unit), elemty, nelems);

			eat(p, "end array", tok_rsquare);
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

static type *parse_type_maybe_func(parse *p, dynarray *toplvl_args)
{
	type *t = parse_type_maybe_func_nochk(p, toplvl_args);

	if(type_is_fn(type_array_element(t))){
		sema_error(p, "array of functions");
		return default_type(p);
	}

	if(type_array_element(type_func_call(t, NULL))){
		sema_error(p, "function returning array");
		return default_type(p);
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

		eat(p, "ident", tok_ident);

		return v;
	}

	/* need a type and a literal, e.g. i32 5 */
	ty = parse_type(p);

	if(!ty){
		parse_error(p, "value type expected, got %s",
				token_to_str(token_peek(p->tok)));

		ty = default_type(p);
	}

	if(type_is_void(ty))
		return val_new_void(unit_uniqtypes(p->unit));

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
	dynarray *argtys;
	bool tyerror = false;
	type *retty = NULL;
	size_t i;

	target = parse_val(p);

	type *ptr = type_deref(val_type(target));
	if(ptr)
		retty = type_func_call(ptr, &argtys);

	if(!retty){
		retty = default_type(p);
		tyerror = true; /* disable argument type checks */
		sema_error(p, "call requires function (pointer) operand (got %s)",
				type_to_str(val_type(target)));
	}

	if(ident_or_null){
		into = uniq_val(p, ident_or_null, retty, VAL_CREATE);
		if(type_is_void(retty)){
			sema_error(p, "using void result of call");
			retty = type_get_primitive(unit_uniqtypes(p->unit), i4);
		}
	}else{
		into = NULL;
		/* retty may be non-void - discarded */
	}

	eat(p, "call paren", tok_lparen);

	for(i = 0; ; i++){
		val *arg;

		if(dynarray_is_empty(&args)){
			if(token_peek(p->tok) == tok_rparen)
				break; /* call x() */
		}else{
			eat(p, "call comma", tok_comma);
		}

		arg = parse_val(p);

		dynarray_add(&args, arg);

		if(!tyerror){
			if(i < dynarray_count(argtys)){
				type *argty = dynarray_ent(argtys, i);

				if(argty != val_type(arg)){
					char buf[256];

					sema_error(p, "argument %zu mismatch (%s passed to %s)",
							i + 1,
							type_to_str_r(buf, sizeof buf, val_type(arg)),
							type_to_str(argty));
				}
			}else{
				sema_error(p, "too many arguments to function");
			}
		}

		if(token_peek(p->tok) == tok_rparen || parse_finished(p->tok))
			break;
	}

	if(i + 1 < dynarray_count(argtys)){
		sema_error(p, "too few arguments to function");
	}

	eat(p, "call paren", tok_rparen);

	isn_call(p->entry, into, target, &args);

	dynarray_reset(&args);
}

static bool op_types_valid(type *a, type *b)
{
	return a == b;
}

static void parse_ident(parse *p, char *spel)
{
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
			if(type_array_element(deref_ty)){
				sema_error(p, "load operand is (pointer-to) array type");
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

			if(type_is_fn(ty)){
				sema_error(p, "alloca of function type");
				ty = default_type(p);
			}

			vlhs = uniq_val(
					p, spel,
					type_get_ptr(unit_uniqtypes(p->unit), ty),
					VAL_CREATE | VAL_ALLOCA);

			isn_alloca(p->entry, vlhs);
			break;
		}

		case tok_elem:
		{
			val *vlhs;
			val *index_into;
			val *idx;
			type *array_ty, *element_ty, *resolved_ty;
			struct uniq_type_list *uniqtypes = unit_uniqtypes(p->unit);

			index_into = parse_val(p);

			eat(p, "elem", tok_comma);

			idx = parse_val(p);

			/* given a pointer to an array type,
			 * return a pointer to element `idx' */

			array_ty = type_deref(val_type(index_into));

			if(!array_ty){
				sema_error(p, "elem requires pointer type");

				array_ty = type_get_array(
						uniqtypes,
						type_get_primitive(uniqtypes, i4),
						1);
			}

			element_ty = type_array_element(array_ty);

			if(!element_ty && type_is_struct(array_ty)){
				size_t i;
				if(val_is_int(idx, &i)){
					element_ty = type_struct_element(array_ty, i);
				}
			}

			if(!element_ty){
				sema_error(p, "elem requires (pointer to) array/struct type");
				element_ty = type_get_primitive(uniqtypes, i4);
			}

			resolved_ty = type_get_ptr(uniqtypes, element_ty);

			vlhs = uniq_val(p, spel, resolved_ty, VAL_CREATE);

			isn_elem(p->entry, index_into, idx, vlhs);
			break;
		}

		case tok_ptradd:
		{
			val *vlhs, *vrhs, *vout;

			vlhs = parse_val(p);

			eat(p, "ptradd-comma", tok_comma);

			vrhs = parse_val(p);

			if(!type_deref(val_type(vlhs))){
				sema_error(p, "ptradd requires pointer type (lhs)");
			}
			if(!type_is_int(val_type(vrhs))){
				sema_error(p, "ptradd requires integer type (rhs)");
			}

			vout = uniq_val(p, spel, val_type(vlhs), VAL_CREATE);

			isn_ptradd(p->entry, vlhs, vrhs, vout);
			break;
		}

		case tok_zext:
		{
			val *from;
			val *vres;
			type *ty_to;

			ty_to = parse_type(p);

			if(!type_is_int(ty_to)){
				sema_error(p, "zext requires integer type");
				ty_to = type_get_primitive(unit_uniqtypes(p->unit), iMAX);
			}

			eat(p, "zext", tok_comma);

			from = parse_val(p);

			if(!type_is_int(val_type(from))){
				sema_error(p, "zext argument requires integer type");
			}

			vres = uniq_val(p, spel, ty_to, VAL_CREATE);

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

				if(!op_types_valid(val_type(vlhs), val_type(vrhs))){
					sema_error(p, "mismatching types in op");
				}

				opty = (is_cmp
						? type_get_primitive(unit_uniqtypes(p->unit), i1)
						: val_type(vlhs));

				vres = uniq_val(p, spel, opty, VAL_CREATE);

				if(is_cmp)
					isn_cmp(p->entry, cmp, vlhs, vrhs, vres);
				else
					isn_op(p->entry, op, vlhs, vrhs, vres);

			}else if(tok == tok_ident){
				char *from = token_last_ident(p->tok);
				val *lhs, *rhs;

				rhs = uniq_val(p, from, NULL, 0);
				lhs = uniq_val(p, spel, val_type(rhs), VAL_CREATE);

				isn_copy(p->entry, lhs, rhs);

			}else{
				parse_error(p, "expected load, alloca, elem or operator (got %s)",
						token_to_str(tok));
			}
			break;
		}
	}
}

static void parse_ret(parse *p)
{
	type *expected_ty = type_func_call(function_type(p->func), NULL);
	val *v = parse_val(p);

	if(val_type(v) != expected_ty){
		char buf[256];

		sema_error(p, "mismatching return type (returning %s to %s)",
				type_to_str(val_type(v)),
				type_to_str_r(buf, sizeof buf, expected_ty));
	}

	isn_ret(p->entry, v);
}

static void parse_store(parse *p)
{
	val *lval;
	val *rval;

	lval = parse_val(p);
	eat(p, "store comma", tok_comma);
	rval = parse_val(p);

	if(type_deref(val_type(lval)) != val_type(rval)){
		char buf[256];

		sema_error(p, "store type mismatch (storing %s to %s)",
				type_to_str(val_type(rval)),
				type_to_str_r(buf, sizeof buf, val_type(lval)));
	}

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

	if(!type_is_primitive(val_type(cond), i1)){
		sema_error(p, "br requires 'i1' condition (got %s)",
				type_to_str(val_type(cond)));
	}

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
				parse_ident(p, ident);
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

static void parse_function(
		parse *p,
		char *name, type *ty,
		dynarray *toplvl_args)
{
	function *fn = unit_function_new(p->unit, name, ty, toplvl_args);
	toplvl_args = NULL; /* consumed */

	if(!token_accept(p->tok, tok_lbrace)){
		/* declaration */
		return;
	}

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
	if(!name || unit_global_find(p->unit, name)){
		if(name){
			/* found it */
			sema_error(p, "global '%s' already defined", name);
		}
		name = xstrdup("_error");
	}

	eat(p, "global assign", tok_equal);

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
	/* FIXME: hardcoded pointer and asm info */
	state.unit = unit_new(8, 8, "L");

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
