// A pointer that a thread stores in a heap object reachable from a shared
// pointer.
#include <stdlib.h>

struct node
{
  int *data;
};

struct node *shared;
int a = 1;
int b = 2;

void writer(void)
{
  shared->data = &b;
}

int main()
{
  shared = malloc(sizeof(struct node));
  shared->data = &a;
__CPROVER_ASYNC_1:
  writer();
  int *d = shared->data;
  if(d != &a)
  {
    __CPROVER_assert(*d == 2, "b");
  }
  __CPROVER_assert(*d == 1, "b is reachable");
  return 0;
}
