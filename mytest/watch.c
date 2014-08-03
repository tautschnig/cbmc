int g1 = 100; // global
int g2 = 20; //global

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
  int i;
  
  foo(x, y);
  
  for(i = 0; i < 3; i++)
  {
    x++;
    y--;
  }

  return 0;
}