#include <stdio.h>

#include "type.h"
#include "type_iter.h"
#include "type_uniq_struct.h"

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

	uniq_types_init(&uts, 1, 1);

	type *tint = type_get_primitive(&uts, i4);
	test(tint == type_get_primitive(&uts, i4));

	type *tptrint = type_get_ptr(&uts, tint);
	test(tptrint == type_get_ptr(&uts, tint));
}

int main(int argc, const char *argv[])
{
	if(argc != 1){
		fprintf(stderr, "Usage: %s\n", argv[0]);
		return 1;
	}

	test_type_uniq();

	printf("passed: %d, failed: %d\n", passed, failed);

	return !!failed;
}
