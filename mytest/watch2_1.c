#include <stdio.h>

int g1 = 100; // global
int g2 = 20; //global

extern int foo2(int a);

void foo(int a, int b)
{
  int x = 1;
  int y = 2;

  g1++;
  g2--;
  
  if (b > 0)
    foo(a + 1, b - 1);
    
  return;
 
}

int main()
{
  int x = 5;
  int y = 3;

  printf("y=%d\n", y);
  
  foo(x, y);
  
  x = foo2(y);
  
  printf("x=%d\n", x);
  
  return 0;
}