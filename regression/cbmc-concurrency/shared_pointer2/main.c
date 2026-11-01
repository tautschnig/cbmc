// A local copy of a shared pointer that another thread writes.
#include <stdlib.h>

int *p;

void writer(void)
{
  int *q = malloc(sizeof(int));
  *q = 42;
  p = q;
}

int main()
{
  int x = 1;
  p = &x;
__CPROVER_ASYNC_1:
  writer();
  int *local = p;
  int v = *local;
  __CPROVER_assert(v == 1 || v == 42, "x or the heap object");
  __CPROVER_assert(v == 1, "the heap object is reachable");
  return 0;
}
