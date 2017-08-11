extern int printf(const char *, ...)
	__attribute__((format(printf, 1, 2)));

typedef struct {
	int a, b;
	char *s, *s2;
} A;

extern A f(void) __asm__("f");

int main()
{
	A a = f();

	printf("{ %d, %d, \"%s\", \"%s\" } (should be 1, 2)\n",
			a.a, a.b,
			a.s, a.s2);
}
