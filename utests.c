#include <stdio.h>

#include "type.h"
#include "type_iter.h"
#include "uniq_type_list.h"
#include "uniq_type_list_struct.h"
#include "reg.h"

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

	for(type **i = expected; *i; i++){
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

int main(int argc, const char *argv[])
{
	if(argc != 1){
		fprintf(stderr, "Usage: %s\n", argv[0]);
		return 1;
	}

	test_type_uniq();
	test_type_iter();
	test_regt();

	printf("passed: %d, failed: %d\n", passed, failed);

	return !!failed;
}
