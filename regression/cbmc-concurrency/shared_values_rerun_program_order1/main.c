int a = 1, b = 2;
int *p = &a;
int *q = &a;
void copy(void)
{
  q = p;
}
int main()
{
__CPROVER_ASYNC_1:
  copy();
  p = &b;
  int v = *q; // after p = &b: may see b through the thread's copy
  __CPROVER_assert(v == 1, "b is reachable");
  return 0;
}
