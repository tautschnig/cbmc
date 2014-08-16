/* file1.c */
void sort(int[], int n); //forward
void swap(int *, int *); //forward declaration

void foo1(int f1[])
{
  int i;
  i = 5;
  return;
}

void foo2(int *f2)
{
  return;
}


int main(int argc, char **argv)
{
  int aa[10] = {10, 7, 35, 24, 3, 6, 85, 16, 100, 90};
  foo1(aa);
  
	//sort(aa, 10);
  foo2(&aa[1]);
	
	return 0;
}


void sort(int a[], int n)
{
	int i, j;
  i = a[2];
	for (i = n - 1; i >= 1; i--)
	{
		for (j = 0; j < i; j++)
			if (a[j] > a[j+1])
				swap(&a[j], &a[j+1]);
				
		continue;
	}
	
	return;
}

void swap(int *a, int *b)
{
	int temp = *a;
	*a = *b;
	*b = temp;
	
	return;
}
