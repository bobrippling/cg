extern int f(void) __asm("f");
extern int printf(const char *, ...);
extern int atoi(const char *);

extern int x __asm("x");

int main(int argc, char **argv)
{
#ifdef HAVE_PHI
	x = argv[1] ? atoi(argv[1]) : 3;
#else
	if(argv[1])
		x = atoi(argv[1]);
	else
		x = 3;
#endif
	printf("x=%d\n", x);
	printf("f()=%d\n", f());
}
