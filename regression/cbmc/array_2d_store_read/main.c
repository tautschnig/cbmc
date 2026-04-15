int main()
{
  unsigned n, m;
  __CPROVER_assume(n > 1 && n < 4 && m > 1 && m < 4);
  int a[n][m];
  a[1][0] = 42;
  int x = a[1][0];
  __CPROVER_assert(x == 42, "store-read on 2D VLA");
}
