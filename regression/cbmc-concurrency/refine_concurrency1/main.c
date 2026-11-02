int x;

void writer(void)
{
  x = 1;
  x = 2;
}

int main()
{
__CPROVER_ASYNC_1:
  writer();
  int a = x;
  int b = x;
  // Coherence: after reading the second write, the first one cannot be read.
  __CPROVER_assert(!(a == 2 && b == 1), "coherent");
  __CPROVER_assert(!(a == 1 && b == 2), "both writes may be seen");
  return 0;
}
