#include <stdlib.h>

_Bool __CPROVER_shadow_memory_incomplete;
int *shared;

void producer(void)
{
  int *p = malloc(sizeof(int));
  __CPROVER_assume(p);
  shared = p;
}

int main()
{
  __CPROVER_field_decl_global("f", (_Bool)0);
__CPROVER_ASYNC_1:
  producer();
  int *q = shared;
  if(q != 0)
  {
    // The object was allocated by a thread that symbolic execution executes
    // after this one.
    __CPROVER_set_field(q, "f", 1);
    __CPROVER_assert(
      !__CPROVER_shadow_memory_incomplete, "shadow memory resolved");
    __CPROVER_assert(__CPROVER_get_field(q, "f") == 1, "set");
  }
  return 0;
}
