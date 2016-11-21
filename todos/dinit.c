int x = 5;

extern int y_EXTERN;

const char str_CONST[] = "hello";

static char str2_STATIC[] = { 104, 101, 108, 108, 111, 0 };

__attribute__((weak))
struct A { int a; short s; } s_WEAK = { 1, 3 };

struct { int a; long l[3]; } as[] = {
	{ 1, { 1, 2, 3 } },
	{ 2, { 4, 5, 6 } },
};

char *f()
{
	return str2_STATIC;
}
