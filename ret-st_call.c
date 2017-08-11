extern int printf(const char *, ...);

typedef struct {
	int a, b;
} A;

extern A f(void) __asm__("f");

int main()
{
	A a = f();

	printf("{ %d, %d } (should be 1, 2)\n", a.a, a.b);
}
