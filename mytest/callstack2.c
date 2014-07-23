void foo_b()
{
	int y;
	y = 2;
	return;
}

void foo_a()
{
	int x;
	foo_b();
	x = 1;
	return;
}

int main()
{
	int a;
	int b;
	
	a = 1;
	
	foo_a();
	
	b = 2;

	return a;
}