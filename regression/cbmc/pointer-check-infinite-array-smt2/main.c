int a[__CPROVER_constant_infinity_uint];
int main()
{
  int *p = a;
  unsigned i;
  __CPROVER_assume(i < 10);
  p[i] = 1;
  __CPROVER_assert(p[i] == 1, "x");
}
