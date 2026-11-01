// A thread executed later (in symbolic execution) than the reader writes a
// pointer to a shared object: the reader's dereference must consider it.
int x;
int y = 1;
int *p = &x;

void writer(void)
{
  p = &y;
}

int main()
{
__CPROVER_ASYNC_1:
  writer();
  int v = *p;
  __CPROVER_assert(v == 0 || v == 1, "x or y");
  __CPROVER_assert(v == 0, "y is reachable");
  return 0;
}
