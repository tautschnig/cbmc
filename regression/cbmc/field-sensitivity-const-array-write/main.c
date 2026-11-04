// Arrays of const-qualified elements are not divided into their elements
// by field sensitivity. Casting away const and writing to such an object
// is undefined behaviour (C11 6.7.3p6), but compilers accept it: the
// writes must still be seen.
#include <assert.h>

static const int table[4] = {1, 2, 3, 4};

int main(void)
{
  int *p = (int *)table;
  unsigned i;
  __CPROVER_assume(i < 4);
  p[i] = 10;
  p[1] = 20;
  assert(table[1] == 20);
  assert(i == 1 || table[i] == 10);
  assert(table[0] == 1); // fails when i == 0
  return 0;
}
