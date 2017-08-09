extern int *zero(int *, long);
extern int printf(const char *, ...);

static void dump(int *p, int n)
{
	printf("{ ");
	const char *join = "";
	for(int i = 0; i < n / sizeof(p[0]); i++){
		printf("%s%d", join, p[i]);
		join = ", ";
	}
	printf(" }\n");
}

int main()
{
	int ents[] = { 1, 2, 3, 4 };

	dump(ents, sizeof(ents));
	for(int i = 0; i < 4; i++){
		zero(ents, i);
		dump(ents, sizeof(ents));
	}
}
