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

	parse_error_fn *error_v;
	void *error_ctx;
} parse;

enum val_opts
{
	VAL_CREATE = 1 << 0,
	VAL_ALLOCA = 1 << 1,
	VAL_LABEL = 1 << 2,
};

static type *parse_type(parse *);


attr_printf(2, 0)
static void error_v(parse *p, const char *fmt, va_list l)
{
	char buf[32];
	size_t off;

	if(p->error_v){
		p->error_v(token_curfile(p->tok), token_curlineno(p->tok), p->error_ctx, fmt, l);
		return;
	}

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
	const char *const name_to_print = name;

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
				parse_error(p, "pre-existing identifier '%s'", name_to_print);

			free(name);

			return v;
		}
	}

	/* check args */
	if(function_arg_find(p->func, name, &arg_idx, &arg_ty)){
		v = val_new_argument(name, arg_ty);

		function_register_arg_val(p->func, arg_idx, v);

		map_val(p, name, v);

		name = NULL;

		goto found;
	}

	/* check globals */
	glob = unit_global_find(p->unit, name);

	if(glob){
		v = val_new_global(unit_uniqtypes(p->unit), glob);
		goto found;
	}

	if((opts & VAL_CREATE) == 0){
		parse_error(p, "undeclared identifier '%s'", name_to_print);
		ty = default_type(p);
	}

	if(opts & VAL_LABEL)
		v = val_new_label(name, ty);
	else
		v = val_new_local(name, ty, opts & VAL_ALLOCA);

	return map_val(p, name, v);
}

static void sema_error_if_no_global_ident(parse *p, const char *ident, type **const tout)
{
	global *glob = unit_global_find(p->unit, ident);

	*tout = NULL;

	if(glob){
		*tout = global_type_as_ptr(unit_uniqtypes(p->unit), glob);
	}else{
		sema_error(p, "no such (global) identifier \"%s\"", ident);
		*tout = default_type(p);
	}
}

static void eat(parse *p, const char *desc, enum token expect)
{
	enum token got = token_next(p->tok);
	if(got == expect)
		return;

	parse_error(p, "expected %s%s%s, got %s",
			token_to_str(expect),
			desc ? " for " : "",
			desc ? desc : "",
			token_to_str(got));
}

static int parse_finished(tokeniser *tok)
{
	return token_peek(tok) == tok_eof || token_peek(tok) == tok_unknown;
}

static void parse_type_list(
		parse *p, dynarray *types,
		dynarray *toplvl_args,
		bool *const variadic,
		enum token lasttok)
{
	if(variadic)
		*variadic = false;


	if(token_accept(p->tok, tok_ellipses)){
		*variadic = true;

	}else if(token_peek(p->tok) != lasttok){
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

			if(token_accept(p->tok, tok_comma)){
				if(variadic && token_accept(p->tok, tok_ellipses)){
					*variadic = true;
				}else{
					/* , xyz */
					continue;
				}
			}
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
	 * $typename
	 */
	type *t = NULL;
	enum token tok;

	switch((tok = token_next(p->tok))){
			enum type_primitive prim;

		case tok_ident:
		{
			char *spel = token_last_ident(p->tok);

			t = type_alias_find(unit_uniqtypes(p->unit), spel);

			if(!t){
				parse_error(p, "no such type '%s'", spel);
				t = default_type(p);
			}

			free(spel);
			break;
		}

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

			parse_type_list(p, &types, NULL, NULL, tok_rbrace);

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
			bool variadic;

			parse_type_list(p, &types, toplvl_args, &variadic, tok_rparen);

			t = type_get_func(unit_uniqtypes(p->unit), t, &types, variadic);
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

	if(type_array_element(type_func_call(t, NULL, NULL))){
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
		const char *peek_ident = token_last_ident_peek(p->tok);

		if(type_alias_find(unit_uniqtypes(p->unit), peek_ident)){
			/* we're at the beginning of a type, not an identifier */
		}else{
			val *v = uniq_val(p, token_last_ident(p->tok), NULL, 0);

			eat(p, "ident", tok_ident);

			return v;
		}
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

	if(token_accept(p->tok, tok_undef))
		return val_new_undef(ty);

	parse_error(p, "value operand expected, got %s",
			token_to_str(token_peek(p->tok)));

	return val_new_i(0, ty);
}

static void parse_call(parse *p, char *ident)
{
	val *target;
	val *into;
	dynarray args = DYNARRAY_INIT;
	dynarray *argtys;
	bool variadic = false;
	bool tyerror = false;
	type *retty = NULL;
	bool stret;
	size_t i;

	target = parse_val(p);

	type *ptr = type_deref(val_type(target));
	if(ptr)
		retty = type_func_call(ptr, &argtys, &variadic);

	if(!retty){
		retty = default_type(p);
		tyerror = true; /* disable argument type checks */
		sema_error(p, "call requires function (pointer) operand (got %s)",
				type_to_str(val_type(target)));
	}

	stret = type_is_struct(retty);
	if(stret){
		retty = type_get_ptr(unit_uniqtypes(p->unit), retty);
	}

	into = uniq_val(p, ident, retty, VAL_CREATE | (stret ? VAL_ALLOCA : 0));
	/* void results are fine */

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
			}else if(variadic){
				/* fine */
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

	block_add_isn(p->entry, isn_call(into, target, &args));

	dynarray_reset(&args);
}

static bool cmp_types_valid(enum op_cmp cmp, type *a, type *b)
{
	return type_eq(a, b);
}

static bool op_types_valid(enum op op, type *a, type *b)
{
	return (type_is_int(a) || type_is_float(a, true)) && type_eq(a, b);
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
			block_add_isn(p->entry, isn_load(lhs, rhs));
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

			block_add_isn(p->entry, isn_alloca(vlhs));
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
						default_type(p),
						1);
			}

			element_ty = type_array_element(array_ty);
			if(!element_ty && type_is_struct(array_ty)){
				size_t i;
				if(val_is_int(idx, &i)){
					element_ty = type_struct_element(array_ty, i);

					if(!element_ty){
						sema_error(p, "elem index out of struct bounds");
						element_ty = default_type(p);
					}
				}
			}

			if(!element_ty){
				sema_error(p, "elem requires (pointer to) array/struct type");
				element_ty = default_type(p);
			}

			if(type_size(val_type(idx)) != type_size(type_get_sizet(unit_uniqtypes(p->unit)))){
				sema_error(p, "elem (array-based) requires pointer-sized integer type (rhs)");
			}

			resolved_ty = type_get_ptr(uniqtypes, element_ty);

			vlhs = uniq_val(p, spel, resolved_ty, VAL_CREATE);

			block_add_isn(p->entry, isn_elem(index_into, idx, vlhs));
			break;
		}

		case tok_ptradd:
		case tok_ptrsub:
		{
			val *vlhs, *vrhs, *vout;

			vlhs = parse_val(p);

			eat(p, "ptradd/sub-comma", tok_comma);

			vrhs = parse_val(p);

			if(!type_deref(val_type(vlhs))){
				sema_error(p, "ptradd/sub requires pointer type (lhs)");
			}
			if(tok == tok_ptradd){
				if(!type_is_int(val_type(vrhs))){
					sema_error(p, "ptradd requires integer type (rhs)");
				}
				if(type_size(val_type(vrhs)) != type_size(type_get_sizet(unit_uniqtypes(p->unit)))){
					sema_error(p, "ptradd requires pointer-sized integer type (rhs)");
				}
			}else{
				if(!type_eq(val_type(vlhs), val_type(vrhs))){
					sema_error(p, "ptrsub type mismatch");
				}
			}
			if(type_is_void(type_deref(val_type(vlhs)))){
				sema_error(p, "can't increment/decrement void*");
			}

			vout = uniq_val(
					p,
					spel,
					tok == tok_ptradd
					? val_type(vlhs)
					: type_get_sizet(unit_uniqtypes(p->unit)),
					VAL_CREATE);

			block_add_isn(p->entry,
					(tok == tok_ptradd
					? isn_ptradd
					: isn_ptrsub)(vlhs, vrhs, vout));
			break;
		}

		case tok_zext:
		case tok_sext:
		case tok_trunc:
		{
			val *from;
			val *vres;
			type *ty_to;
			isn *(*isn_make)(val *from, val *to);
			unsigned sz_from, sz_to;
			int extend = 1;

			ty_to = parse_type(p);

			if(!type_is_int(ty_to)){
				sema_error(p, "ext/trunc requires integer type");
				ty_to = type_get_primitive(unit_uniqtypes(p->unit), iMAX);
			}

			eat(p, "ext/trunc", tok_comma);

			from = parse_val(p);

			if(!type_is_int(val_type(from))){
				sema_error(p, "ext/trunc argument requires integer type");
			}

			vres = uniq_val(p, spel, ty_to, VAL_CREATE);

			sz_from = type_size(val_type(from));
			sz_to = type_size(ty_to);

			switch(tok){
				case tok_sext:
					isn_make = isn_sext;
					break;
				case tok_zext:
					isn_make = isn_zext;
					break;
				case tok_trunc:
					isn_make = isn_trunc;
					extend = 0;
					break;
				default:
					assert(0 && "unreachable");
			}

			if(!(extend ? sz_from < sz_to : sz_from > sz_to)){
				sema_error(
						p,
						"%s has incorrect operand sizes, from=%d, to=%d",
						extend ? "extend" : "truncate",
						sz_from,
						sz_to);
			}

			block_add_isn(p->entry, isn_make(from, vres));
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
				uniq_type_list *utl = unit_uniqtypes(p->unit);

				if(is_cmp){
					if(!cmp_types_valid(cmp, val_type(vlhs), val_type(vrhs))){
						sema_error(p, "mismatching types in cmp");
					}
				}else{
					switch(op){
						default:
							if(!op_types_valid(op, val_type(vlhs), val_type(vrhs))){
								sema_error(p, "invalid types for op");
							}
							break;
						case op_shiftl:
						case op_shiftr_logic:
						case op_shiftr_arith:
							break;
					}
				}

				if(is_cmp){
					opty = type_get_primitive(utl, i1);
				}else if(tok == tok_ptrsub){
					opty = type_get_sizet(utl);
				}else{
					opty = val_type(vlhs);
				}

				vres = uniq_val(p, spel, opty, VAL_CREATE);

				if(is_cmp)
					block_add_isn(p->entry, isn_cmp(cmp, vlhs, vrhs, vres));
				else
					block_add_isn(p->entry, isn_op(op, vlhs, vrhs, vres));

			}else if(tok == tok_ident){
				char *from = token_last_ident(p->tok);
				val *lhs, *rhs;

				rhs = uniq_val(p, from, NULL, 0);
				lhs = uniq_val(p, spel, val_type(rhs), VAL_CREATE);

				block_add_isn(p->entry, isn_copy(lhs, rhs));

			}else{
				parse_error(p, "expected load, alloca, elem or operator (got %s)",
						token_to_str(tok));
			}
			break;
		}

		case tok_ptr2int:
		case tok_int2ptr:
		case tok_ptrcast:
		{
			type *to = parse_type(p);
			val *input;
			val *vres;

			if(tok == tok_ptr2int ? type_is_int(to) : !!type_deref(to)){
				/* fine */
			}else{
				sema_error(p, "%s type expected for %s",
						tok == tok_int2ptr ? "integer" : "pointer",
						token_to_str(tok));
			}

			eat(p, "comma", tok_comma);

			input = parse_val(p);

			vres = uniq_val(p, spel, to, VAL_CREATE);

			if(tok == tok_ptr2int)
				block_add_isn(p->entry, isn_ptr2int(input, vres));
			else if(tok == tok_int2ptr)
				block_add_isn(p->entry, isn_int2ptr(input, vres));
			else if(tok == tok_ptrcast)
				block_add_isn(p->entry, isn_ptrcast(input, vres));
			else
				assert(0 && "unreachable");
			break;
		}
	}
}

static bool permitted_to_return(type *expr, type *to)
{
	if(type_is_struct(to)){
		return type_eq(type_deref(expr), to);
	}else{
		return type_eq(expr, to);
	}
}

static void parse_ret(parse *p)
{
	type *expected_ty = type_func_call(function_type(p->func), NULL, NULL);
	val *v = parse_val(p);

	if(!permitted_to_return(val_type(v), expected_ty)){
		char buf[256];

		sema_error(p, "mismatching return type (returning %s to %s)",
				type_to_str(val_type(v)),
				type_to_str_r(buf, sizeof buf, expected_ty));
	}

	block_add_isn(p->entry, isn_ret(v));
	block_set_type(p->entry, BLK_EXIT);
}

static void parse_store(parse *p)
{
	val *lval;
	val *rval;

	lval = parse_val(p);
	eat(p, "store comma", tok_comma);
	rval = parse_val(p);

	if(!type_eq(type_deref(val_type(lval)), val_type(rval))){
		char buf[256];

		sema_error(p, "store type mismatch (storing %s to %s)",
				type_to_str(val_type(rval)),
				type_to_str_r(buf, sizeof buf, val_type(lval)));
	}

	block_add_isn(p->entry, isn_store(rval, lval));
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

	btrue = function_block_find(p->func, p->unit, ltrue, NULL);
	bfalse = function_block_find(p->func, p->unit, lfalse, NULL);

	block_add_isn(p->entry, isn_br(cond, btrue, bfalse));
	block_set_branch(p->entry, cond, btrue, bfalse);

	enter_unreachable_code(p);
}

static void parse_jmp(parse *p)
{
	if(token_accept(p->tok, tok_star)){
		val *target = parse_val(p);

		block_add_isn(p->entry, isn_jmp_computed(target));
		block_set_type(p->entry, BLK_JMP_COMP);

	}else{
		block *target;
		char *lbl;

		eat(p, "jmp label", tok_ident);
		lbl = token_last_ident(p->tok);

		target = function_block_find(p->func, p->unit, lbl, NULL);

		block_add_isn(p->entry, isn_jmp(target));
		block_set_jmp(p->entry, target);
	}

	enter_unreachable_code(p);
}

static void parse_label(parse *p)
{
	char *lbl;
	uniq_type_list *utl = unit_uniqtypes(p->unit);
	type *blkty = type_get_ptr(utl, type_get_void(utl));
	val *blkval;

	eat(p, "label decl", tok_ident);
	lbl = token_last_ident(p->tok);

	blkval = uniq_val(p, xstrdup(lbl), blkty, VAL_CREATE | VAL_LABEL);
	block_add_isn(p->entry, isn_label(blkval));

	function_block_find(p->func, p->unit, lbl, NULL);
}

static void parse_asm(parse *p)
{
	struct string str;

	eat(p, "asm string", tok_string);

	token_last_string(p->tok, &str);

	block_add_isn(p->entry, isn_asm(&str));
}

static void parse_memcpy(parse *p)
{
	val *dest, *src;

	dest = parse_val(p);
	eat(p, "memcpy", tok_comma);
	src = parse_val(p);

	if(!type_eq(val_type(dest), val_type(src))){
		sema_error(p, "mismatching memcpy types");
	}else if(!type_deref(val_type(dest))){
		sema_error(p, "memcpy type is not a pointer");
	}

	block_add_isn(p->entry, isn_memcpy(dest, src));
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
				block *from = p->entry;

				eat(p, "label colon", tok_colon);

				p->entry = function_block_find(p->func, p->unit, xstrdup(ident), &created);

				if(!created && !block_tenative(p->entry)){
					parse_error(p, "block '%s' already exists", ident);

					/* use an anonymous block to prevent assertion failures
					 * in the backend */
					enter_unreachable_code(p);
				}

				free(ident), ident = NULL;

				if(p->entry && from && block_unknown_ending(from)){
					/* current block is fall-thru */
					block_add_isn(from, isn_jmp(p->entry));
					block_set_jmp(from, p->entry);
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

		case tok_label:
			parse_label(p);
			break;

		case tok_asm:
			parse_asm(p);
			break;

		case tok_memcpy:
			parse_memcpy(p);
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
	enum function_attributes attr = 0;

	/* look for weak (and other attributes) */
	for(;;){
		if(token_accept(p->tok, tok_internal)){
			attr |= function_attribute_internal;
		}else if(token_accept(p->tok, tok_bareword)){
			char *bareword = token_last_bareword(p->tok);

			if(!strcmp(bareword, "weak"))
				attr |= function_attribute_weak;
			else
				parse_error(p, "unknown function modifier '%s'", bareword);

			free(bareword);
		}else{
			break;
		}
	}

	function_add_attributes(fn, attr);

	if(!token_accept(p->tok, tok_lbrace)){
		/* declaration */
		return;
	}

	p->func = fn;
	p->entry = function_entry_block(fn, true);

	while(token_peek(p->tok) != tok_rbrace && !parse_finished(p->tok)){
		parse_block(p);
	}

	eat(p, "function close brace", tok_rbrace);

	function_finalize(fn);

	dynmap_free(p->names2vals);
	p->names2vals = NULL;
}

static void parse_init_ptr(parse *p, type *ty, struct init *init)
{
	/* $ident [+/- value]
	 * integer literal */
	init->type = init_ptr;

	switch(token_peek(p->tok)){
		case tok_ident:
		{
			char *ident;
			enum op op;
			long offset = 0;
			type *ident_ty;
			int anyptr = 0;

			eat(p, "pointer initialiser", tok_ident);
			ident = token_last_ident(p->tok);

			sema_error_if_no_global_ident(p, ident, &ident_ty);

			if(token_is_op(token_peek(p->tok), &op)){
				switch(op){
					case op_add: offset =  1; break;
					case op_sub: offset = -1; break;
					default:
						parse_error(
								p,
								"invalid pointer initialiser extra: %s",
								op_to_str(op));
				}

				if(offset){
					/* accept add/sub: */
					token_next(p->tok);

					eat(p, "int offset", tok_int);
					offset *= token_last_int(p->tok);
				}
			}

			if(token_accept(p->tok, tok_bareword)){
				char *bareword = token_last_bareword(p->tok);

				if(!strcmp(bareword, "anyptr"))
					anyptr = 1;
				else
					parse_error(p, "unexpected \"%s\"", bareword);

				free(bareword);
			}

			if(!anyptr && ty != ident_ty){
				char buf[128];

				sema_error(p,
						"initialisation type mismatch: init %s with %s",
						type_to_str_r(buf, sizeof(buf), ty),
						type_to_str(ident_ty));
			}

			init->u.ptr.is_label = true;
			init->u.ptr.u.ident.label.ident = ident;
			init->u.ptr.u.ident.label.offset = offset;
			init->u.ptr.u.ident.is_anyptr = anyptr;
			break;
		}

		case tok_int:
			init->u.ptr.is_label = false;
			init->u.ptr.u.integral = token_last_int(p->tok);
			token_next(p->tok);
			break;

		default:
			parse_error(p, "pointer initialiser expected");
			memset(&init->u.ptr, 0, sizeof init->u.ptr);
			return;
	}
}

static struct init *parse_init(parse *p, type *ty)
{
	struct init *init;
	type *subty;

	init = xmalloc(sizeof *init);

	if(token_accept(p->tok, tok_aliasinit)){
		/* aliasinit <type> <init>
		 * (useful for unions) */
		init->type = init_alias;
		init->u.alias.as = parse_type(p);

		if(type_size(init->u.alias.as) > type_size(ty))
			sema_error(p, "aliasinit type size > actual type size");

		init->u.alias.init = parse_init(p, init->u.alias.as);

		return init;
	}

	if((subty = type_array_element(ty))){
		const bool is_string = token_accept(p->tok, tok_string);

		if(!is_string)
			eat(p, "array init open brace", tok_lbrace);

		if(is_string){
			struct string str;
			type *elem = type_array_element(ty);

			token_last_string(p->tok, &str);

			if(!elem || !type_is_primitive(elem, i1)){
				sema_error(p, "init not an i1 array");
			}

			init->type = init_str;
			init->u.str = str;
		}else{
			size_t array_count;

			init->type = init_array;
			dynarray_init(&init->u.elem_inits);

			while(!token_accept(p->tok, tok_eof)){
				struct init *elem = parse_init(p, subty);

				dynarray_add(&init->u.elem_inits, elem);

				if(token_accept(p->tok, tok_rbrace))
					break;

				eat(p, "init comma", tok_comma);

				/* trailing comma: */
				if(token_accept(p->tok, tok_rbrace))
					break;
			}

			/* zero-sized arrays aren't specially handled here */
			array_count = type_array_count(ty);
			if(array_count != dynarray_count(&init->u.elem_inits)){
				sema_error(p, "init count mismatch: %ld vs %ld",
						(long)array_count, (long)dynarray_count(&init->u.elem_inits));
			}
		}

	}else if(type_is_struct(ty)){
		size_t i = 0;

		init->type = init_struct;
		dynarray_init(&init->u.elem_inits);

		eat(p, "init open brace", tok_lbrace);

		for(; !token_accept(p->tok, tok_eof); i++){
			struct init *elem;

			subty = type_struct_element(ty, i);
			if(!subty){
				parse_error(p, "excess struct init");
				break;
			}
			elem = parse_init(p, subty);

			dynarray_add(&init->u.elem_inits, elem);

			if(token_accept(p->tok, tok_rbrace))
				break;

			eat(p, "init comma", tok_comma);

			/* trailing comma: */
			if(token_accept(p->tok, tok_rbrace))
				break;
		}

		if(type_struct_element(ty, i + 1))
			parse_error(p, "too few members for struct init");

	}else if(type_deref(ty)){
		parse_init_ptr(p, ty, init);

	}else{
		/* number */
		eat(p, "int initialiser", tok_int);

		init->type = init_int;
		init->u.i = token_last_int(p->tok);
	}

	return init;
}

static void parse_variable(parse *p, char *name, type *ty)
{
	variable_global *v = unit_variable_new(p->unit, name, ty);
	struct init_toplvl *init_top = NULL;
	struct {
		bool internal, constant, weak;
	} properties = { 0 };

	if(token_accept(p->tok, tok_global)
	|| (properties.internal = token_accept(p->tok, tok_internal)))
	{
		/* accept either "global" or "internal" for linkage and init */
	}
	else
	{
		return;
	}

	/* optional additions */
	while(token_accept(p->tok, tok_bareword)){
		char *bareword = token_last_bareword(p->tok);

		/**/if(!strcmp(bareword, "const"))
			properties.constant = true;
		else if(!strcmp(bareword, "weak"))
			properties.weak = true;
		else
			parse_error(p, "unknown variable modifier '%s'", bareword);

		free(bareword);
	}

	init_top = xmalloc(sizeof *init_top);
	init_top->init = parse_init(p, ty);

	init_top->internal = properties.internal;
	init_top->constant = properties.constant;
	init_top->weak = properties.weak;

	if(init_top)
		variable_global_init_set(v, init_top);
}

static void parse_global(parse *p)
{
	type *ty;
	char *name;
	dynarray toplvl_args = DYNARRAY_INIT;
	global *already;
	int is_type;
	struct typealias *alias;

	/* type $ident = type */
	is_type = token_accept(p->tok, tok_type);

	eat(p, "decl name", tok_ident);
	name = token_last_ident(p->tok);
	if(!name){
		name = xstrdup("_error"); /* error already emitted by eat() */
	}
	else if((already = unit_global_find(p->unit, name))
	&& !global_is_forward_decl(already))
	{
		sema_error(p, "global '%s' already defined", name);
	}
	else
	{
		/* all good - add placeholder type */
		if(is_type)
			alias = type_alias_add(unit_uniqtypes(p->unit), name);
	}

	eat(p, "global assign", tok_equal);

	ty = parse_type_maybe_func(p, &toplvl_args);

	if(is_type){
		/* name consumed above */
		type *completed = type_alias_complete(alias, ty);
		unit_type_new(p->unit, completed);

	}else if(type_is_fn(ty)){
		parse_function(p, name, ty, &toplvl_args);
	}else{
		parse_variable(p, name, ty);
	}
}

static unit *parse_code_internal(
		tokeniser *tok,
		int *const err,
		const struct target *target,
		parse_error_fn on_error,
		void *on_error_ctx)
{
	parse state = { 0 };

	state.tok = tok;
	state.unit = unit_new(target);
	state.error_v = on_error;
	state.error_ctx = on_error_ctx;

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

unit *parse_code(tokeniser *tok, int *const err, const struct target *target)
{
	return parse_code_internal(tok, err, target, NULL, NULL);
}

unit *parse_code_cb(tokeniser *tok, const struct target *target, parse_error_fn on_error, void *ctx)
{
	int err;
	return parse_code_internal(tok, &err, target, on_error, ctx);
}
