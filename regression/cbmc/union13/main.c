#include <assert.h>
#include <stdint.h>

int main()
{
  union {
    intptr_t i;
    int *p;
  } u;
  u.i = 0;
  assert((uintptr_t)u.p == 0); // must not fail
  assert(u.p == (int *)0); // may fail on platforms where NULL is not 0
}
