#include <stdio.h>

int main(int argc, char **argv)
{
	int i = 100;
	int j = 200;
	float f = 12.34f;
	double d = 45.67;
	char c = 'x';
	char msg[] = "Hello";
	
	printf("Hello test");
	printf("i: %d", i);
	printf("j: %d", j);
	printf("f: %.2f", f);
	printf("d: %.2f", d);
	printf("c: %c", c);
	printf("msg: %s", msg);
	printf("i==%d, j==%d, f==%.2f, d==%.2f, c==%c, msg==%s", 
	       i, j, f, d, c, msg);	
	
	return 0;
}
