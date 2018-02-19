#include <stdio.h>
#include <stdint.h>

#include "macros.h"

#include "type.h"
#include "type_iter.h"
#include "uniq_type_list.h"
#include "uniq_type_list_struct.h"
#include "reg.h"
#include "regset.h"
#include "regset_marks.h"
#include "imath.h"

static unsigned failed, passed;

static void test(int cond, const char *strcond)
{
	if(cond){
		passed++;
	}else{
		failed++;
		fprintf(stderr, "test failed: %s\n", strcond);
	}
}

#define test(cond) test((cond), #cond)

static void test_type_uniq(void)
{
	uniq_type_list uts = { 0 };

	uniq_type_list_init(&uts, 1, 1);

	type *tint = type_get_primitive(&uts, i4);
	test(tint == type_get_primitive(&uts, i4));

	type *tptrint = type_get_ptr(&uts, tint);
	test(tptrint == type_get_ptr(&uts, tint));

	uniq_type_list_free(&uts);
}

static type *create_nested_struct_ty(uniq_type_list *uts)
{
	dynarray submembs = DYNARRAY_INIT;
	dynarray_add(&submembs, type_get_primitive(uts, i4));
	dynarray_add(&submembs, type_get_primitive(uts, i8));
	type *substruct = type_get_struct(uts, &submembs);

	dynarray membs = DYNARRAY_INIT;
	dynarray_add(&membs, type_get_primitive(uts, i1));
	dynarray_add(&membs, type_get_ptr(uts, type_get_primitive(uts, i2)));
	dynarray_add(&membs, substruct);
	dynarray_add(&membs, type_get_primitive(uts, i2));
	type *tstruct = type_get_struct(uts, &membs);
	return tstruct;
}

static void test_type_iter(void)
{
	uniq_type_list uts = { 0 };

	uniq_type_list_init(&uts, 1, 1);

	type *tstruct = create_nested_struct_ty(&uts);

	type_iter *iter = type_iter_new(tstruct);

	type *expected[] = {
		type_get_primitive(&uts, i1),
		type_get_ptr(&uts, type_get_primitive(&uts, i2)),
		type_get_primitive(&uts, i4),
		type_get_primitive(&uts, i8),
		type_get_primitive(&uts, i2),
		NULL
	};

	type **i;
	for(i = expected; *i; i++){
		type *got = type_iter_next(iter);

		test(got == *i);
	}

	type_iter_free(iter);
	uniq_type_list_free(&uts);
}

static void test_regt(void)
{
	regt a = regt_make(0x5678, 0);
	regt b = regt_make(0x5, 1);

	test(!regt_is_fp(a));
	test(regt_is_int(a));
	test(regt_index(a) == 0x5678);

	test(regt_is_fp(b));
	test(!regt_is_int(b));
	test(regt_index(b) == 5);
}

static void test_regset(void)
{
	const regt regs[] = {
		regt_make(0, false),
		regt_make(1, false),
		regt_make(2, false)
	};
	struct regset set = { regs, countof(regs) };

	test(regset_int_count(&set) == 3);
	test(regset_fp_count(&set) == 0);

	test(regset_nth(&set, 1, false) == regs[1]);
}

static void test_regset_mark(void)
{
	regset_marks marks = regset_marks_new();
	const regt regs[] = {
		regt_make(0, true),
		regt_make(7, true),
		regt_make(7, false),
		regt_make(15, false)
	};

	/* mark:
	 * {0,true}
	 * {7,true}
	 * {7,false}
	 * {15,false}
	 */
	regset_mark(marks, regs[0], true);
	regset_mark(marks, regs[1], true);
	regset_mark(marks, regs[2], true);
	regset_mark(marks, regs[3], true);

	test(regset_is_marked(marks, regs[0]));
	test(regset_is_marked(marks, regs[1]));
	test(regset_is_marked(marks, regs[2]));
	test(regset_is_marked(marks, regs[3]));

	test(!regset_is_marked(marks, regt_make(0, false)));
	test(!regset_is_marked(marks, regt_make(1, false)));
	test(!regset_is_marked(marks, regt_make(2, false)));
	test(!regset_is_marked(marks, regt_make(3, false)));

	test(!regset_is_marked(marks, regt_make(1, true)));
	test(!regset_is_marked(marks, regt_make(2, true)));
	test(!regset_is_marked(marks, regt_make(3, true)));
	test(!regset_is_marked(marks, regt_make(15, true)));

	regset_mark(marks, regs[1], false);
	regset_mark(marks, regs[2], false);

	test(regset_is_marked(marks, regs[0]));
	test(!regset_is_marked(marks, regs[1]));
	test(!regset_is_marked(marks, regs[2]));
	test(regset_is_marked(marks, regs[3]));

	regset_marks_free(marks);
}

static int int_cmp(const void *va, const void *vb)
{
	intptr_t a = (intptr_t)va;
	intptr_t b = (intptr_t)vb;
	return b - a;
}

static void test_dynarray(void)
{
	dynarray d = DYNARRAY_INIT;

	dynarray_add(&d, (void *)(intptr_t)1);
	dynarray_add(&d, (void *)(intptr_t)2);
	dynarray_add(&d, (void *)(intptr_t)3);
	dynarray_add(&d, (void *)(intptr_t)4);
	dynarray_add(&d, (void *)(intptr_t)5);
	dynarray_add(&d, (void *)(intptr_t)6);

	dynarray_splice(&d, 2, 2);

	test(dynarray_count(&d) == 4);
	test(dynarray_ent(&d, 0) == (void *)1);
	test(dynarray_ent(&d, 1) == (void *)2);
	test(dynarray_ent(&d, 2) == (void *)5);
	test(dynarray_ent(&d, 3) == (void *)6);

	dynarray_sort(&d, int_cmp);

	test(dynarray_count(&d) == 4);
	test(dynarray_ent(&d, 0) == (void *)6);
	test(dynarray_ent(&d, 1) == (void *)5);
	test(dynarray_ent(&d, 2) == (void *)2);
	test(dynarray_ent(&d, 3) == (void *)1);

	dynarray_reset(&d);

	dynarray_sort(&d, int_cmp);
}

static void test_align(void)
{
	test(gap_for_alignment(0, 1) == 0);
	test(gap_for_alignment(0, 2) == 0);
	test(gap_for_alignment(0, 4) == 0);
	test(gap_for_alignment(0, 8) == 0);

	test(gap_for_alignment(1,  1) == 0);
	test(gap_for_alignment(2,  1) == 0);
	test(gap_for_alignment(3,  1) == 0);
	test(gap_for_alignment(4,  1) == 0);
	test(gap_for_alignment(8,  1) == 0);
	test(gap_for_alignment(9,  1) == 0);
	test(gap_for_alignment(13, 1) == 0);

	test(gap_for_alignment(1,  2) == 1);
	test(gap_for_alignment(2,  2) == 0);
	test(gap_for_alignment(3,  2) == 1);
	test(gap_for_alignment(4,  2) == 0);
	test(gap_for_alignment(8,  2) == 0);
	test(gap_for_alignment(9,  2) == 1);
	test(gap_for_alignment(13, 2) == 1);

	test(gap_for_alignment(1,  4) == 3);
	test(gap_for_alignment(2,  4) == 2);
	test(gap_for_alignment(3,  4) == 1);
	test(gap_for_alignment(4,  4) == 0);
	test(gap_for_alignment(8,  4) == 0);
	test(gap_for_alignment(9,  4) == 3);
	test(gap_for_alignment(13, 4) == 3);

	test(gap_for_alignment(1,  8) == 7);
	test(gap_for_alignment(2,  8) == 6);
	test(gap_for_alignment(3,  8) == 5);
	test(gap_for_alignment(4,  8) == 4);
	test(gap_for_alignment(8,  8) == 0);
	test(gap_for_alignment(9,  8) == 7);
	test(gap_for_alignment(13, 8) == 3);
}

int main(int argc, const char *argv[])
{
	if(argc != 1){
		fprintf(stderr, "Usage: %s\n", argv[0]);
		return 1;
	}

	test_type_uniq();
	test_type_iter();
	test_regt();
	test_regset();
	test_regset_mark();
	test_dynarray();
	test_align();

	printf("passed: %d, failed: %d\n", passed, failed);

	return !!failed;
}
