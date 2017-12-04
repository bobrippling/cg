#include <assert.h>

#include "reg.h"
#include "isn.h"
#include "type.h"
#include "imath.h"
#include "val.h"
#include "isn_struct.h"
#include "function.h"
#include "unit.h"

#include "builtins.h"

static void insert_new(isn **const iterator, isn *new)
{
	isn_insert_after(*iterator, new);
	*iterator = new;
}

static isn *expand_memcpy_small(
		int any_id,
		unsigned len,
		val *src,
		val *dest,
		uniq_type_list *utl)
{
	type *ty = type_get_ptr(utl, type_get_primitive(utl, type_primitive_less_or_equal(len, false)));
	val *dcasted = val_new_localf(ty, false, "memcp_%d_d", any_id);
	val *scasted = val_new_localf(ty, false, "memcp_%d_s", any_id);
	val *loaded = val_new_localf(type_deref(ty), false, "memcp_%d_v", any_id);
	isn *insertion = isn_ptrcast(dest, dcasted);
	insert_new(&insertion, isn_ptrcast(src, scasted));
	insert_new(&insertion, isn_load(loaded, scasted));
	insert_new(&insertion, isn_store(loaded, dcasted));
	return isn_first(insertion);
}

static void expand_memcpy_large(
		isn *const isn_memcpy,
		block *const blk_current,
		unsigned len,
		val *const src,
		val *const dest,
		unit *const unit,
		function *const fn)
{
	int uniq = 0;
	block *blk_init = function_block_new(fn, unit);
	block *blk_loop = function_block_new(fn, unit);
	block *blk_copy = function_block_new(fn, unit);
	block *blk_fin;
	uniq_type_list *utl = unit_uniqtypes(unit);
	type *t_i1 = type_get_primitive(utl, i1);
	type *t_sizet = type_get_sizet(utl);
	type *t_sizet_ptr = type_get_ptr(utl, t_sizet);
	type *t_sizet_ptrptr = type_get_ptr(utl, t_sizet_ptr);
	unsigned remainder;

	val *cnt = val_new_localf(t_sizet_ptr, true, "cnt_%d.%d", (int)isn_memcpy, uniq++);
	val *a = val_new_localf(t_sizet_ptrptr, true, "a_%d.%d", (int)isn_memcpy, uniq++);
	val *b = val_new_localf(t_sizet_ptrptr, true, "b_%d.%d", (int)isn_memcpy, uniq++);
	val *a_ = dest;
	val *b_ = src;
	val *a_casted = val_new_localf(t_sizet_ptr, false, "a_casted_%d.%d", (int)isn_memcpy, uniq++);
	val *b_casted = val_new_localf(t_sizet_ptr, false, "b_casted_%d.%d", (int)isn_memcpy, uniq++);

	val *lcnt = val_new_localf(t_sizet, false, "lcnt_%d.%d", (int)isn_memcpy, uniq++);
	val *done = val_new_localf(t_i1, false, "done_%d.%d", (int)isn_memcpy, uniq++);

	val *p = val_new_localf(t_sizet_ptr, false, "p_%d.%d", (int)isn_memcpy, uniq++);
	val *q = val_new_localf(t_sizet_ptr, false, "q_%d.%d", (int)isn_memcpy, uniq++);
	val *tmp = val_new_localf(t_sizet, false, "tmp_%d.%d", (int)isn_memcpy, uniq++);
	val *p_ = val_new_localf(t_sizet_ptr, false, "p_%d.%d", (int)isn_memcpy, uniq++);
	val *q_ = val_new_localf(t_sizet_ptr, false, "q_%d.%d", (int)isn_memcpy, uniq++);
	val *to_sub = val_new_localf(t_sizet, false, "to_sub_%d.%d", (int)isn_memcpy, uniq++);
	val *subbed = val_new_localf(t_sizet, false, "subbed_%d.%d", (int)isn_memcpy, uniq++);

	isn_insert_before(isn_memcpy, isn_jmp(blk_init));
	function_block_split(fn, unit, blk_current, isn_memcpy, &blk_fin);
	block_set_jmp(blk_current, blk_init);

	block_add_isn(blk_init, isn_alloca(cnt));
	block_add_isn(blk_init, isn_alloca(a));
	block_add_isn(blk_init, isn_alloca(b));
	block_add_isn(blk_init, isn_store(val_new_i(len, t_sizet), cnt));
	block_add_isn(blk_init, isn_ptrcast(a_, a_casted));
	block_add_isn(blk_init, isn_ptrcast(b_, b_casted));
	block_add_isn(blk_init, isn_store(a_casted, a));
	block_add_isn(blk_init, isn_store(b_casted, b));
	block_add_isn(blk_init, isn_jmp(blk_loop));
	block_set_jmp(blk_init, blk_loop);

	block_add_isn(blk_loop, isn_load(lcnt, cnt));
	block_add_isn(blk_loop, isn_cmp(cmp_le, lcnt, val_new_i(type_size(t_sizet), t_sizet), done));
	block_add_isn(blk_loop, isn_br(done, blk_fin, blk_copy));
	block_set_branch(blk_loop, done, blk_fin, blk_copy);

	block_add_isn(blk_copy, isn_load(p, a));
	block_add_isn(blk_copy, isn_load(q, b));
	block_add_isn(blk_copy, isn_load(tmp, q));
	block_add_isn(blk_copy, isn_store(tmp, p));
	/* must manually do ptradd math, since we're already in the isel pass */
	block_add_isn(blk_copy, isn_ptradd(p, val_new_i(type_size(t_sizet), t_sizet), p_));
	block_add_isn(blk_copy, isn_ptradd(q, val_new_i(type_size(t_sizet), t_sizet), q_));
	block_add_isn(blk_copy, isn_store(p_, a));
	block_add_isn(blk_copy, isn_store(q_, b));
	block_add_isn(blk_copy, isn_load(to_sub, cnt));
	block_add_isn(blk_copy, isn_op(op_sub, to_sub, val_new_i(type_size(t_sizet), t_sizet), subbed));
	block_add_isn(blk_copy, isn_store(subbed, cnt));
	block_add_isn(blk_copy, isn_jmp(blk_loop));
	block_set_jmp(blk_copy, blk_loop);

	remainder = len % type_size(t_sizet);
	if(remainder){
		val *loaded_a = val_new_localf(t_sizet_ptr, false, "fixupload_%d.%d", (int)isn_memcpy, uniq++);
		val *loaded_b = val_new_localf(t_sizet_ptr, false, "fixupload_%d.%d", (int)isn_memcpy, uniq++);
		isn *insertion = isn_memcpy;

		insert_new(&insertion, isn_load(loaded_a, a));
		insert_new(&insertion, isn_load(loaded_b, b));
		isns_insert_after(insertion, expand_memcpy_small((int)isn_memcpy, remainder, loaded_b, loaded_a, utl));
	}
}

void builtin_expand_memcpy(isn *isn_memcpy, block *block, function *fn, unit *unit)
{
	uniq_type_list *utl = unit_uniqtypes(unit);
	unsigned tsz;

	if(isn_memcpy->u.memcpy.expanded)
		return;
	isn_memcpy->u.memcpy.expanded = true;

	assert(isn_memcpy->type == ISN_MEMCPY);
	assert(type_eq(val_type(isn_memcpy->u.memcpy.to), val_type(isn_memcpy->u.memcpy.from)));

	tsz = type_size(type_deref(val_type(isn_memcpy->u.memcpy.to)));

	if(tsz == 0){
		;
	}else if(tsz <= type_size(type_get_sizet(utl))){
		isn *copies = expand_memcpy_small((int)isn_memcpy, tsz, isn_memcpy->u.memcpy.from, isn_memcpy->u.memcpy.to, utl);

		isns_insert_after(isn_memcpy, copies);
	}else{
		expand_memcpy_large(
				isn_memcpy,
				block,
				tsz,
				isn_memcpy->u.memcpy.from,
				isn_memcpy->u.memcpy.to,
				unit,
				fn);
	}
}
