#include <stdio.h>

#include "unit.h"
#include "target.h"
#include "tokenise.h"
#include "parse.h"

static unit *compile_string(const char *str, int *const err)
{
	struct target target;
	target_parse("linux-x86_64", &target);

	tokeniser *tok = token_init_str(str);

	return parse_code(tok, err, &target);
}

int main(int argc, const char *argv[])
{
	if(argc != 1){
		fprintf(stderr, "Usage: %s\n", argv[0]);
		return 1;
	}

	int err;
	unit *u = compile_string(
			"$is_5 = i4(i4 $x){"
			"  $b = eq $x, i4 5"
			"  $be = zext i4, $b"
			"  ret $be"
			"}",
			&err);
	if(err){
		fprintf(stderr, "err\n");
		return 1;
	}

	return 0;
}
