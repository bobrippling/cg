int div(int,int) __asm("div");
int printf(const char *, ...);

int main()
{
	int x = div(10, 2);
	printf("%d\n", x);
}
