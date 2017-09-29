#include <stdio.h>
#include <string.h>

#include "strbuf_fixed.h"

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

static void check_overflow(void)
{
	char backing[8];
	strbuf_fixed buf = STRBUF_FIXED_INIT_ARRAY(backing);

	strbuf_fixed_printf(&buf, "hello there");

	test(!strcmp("hello t", strbuf_fixed_str(&buf)));
}

int main(int argc, const char *argv[])
{
	if(argc != 1){
		fprintf(stderr, "Usage; %s\n", argv[0]);
		return 2;
	}

	check_overflow();

	printf("passed: %d, failed: %d\n", passed, failed);
	return !!failed;
}
