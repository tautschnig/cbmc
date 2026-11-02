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
  int v = *q; // before p = &b in program order: cannot see b
  p = &b;
  __CPROVER_assert(v == 1, "only a");
  return 0;
}
