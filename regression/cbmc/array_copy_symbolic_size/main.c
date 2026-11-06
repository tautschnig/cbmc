#include <stdlib.h>

int main()
{
  unsigned n, m, i;
  __CPROVER_assume(n > 0 && n < 5 && m > n && m < 8);
  long *a = malloc(n * sizeof(long));
  long *b = malloc(m * sizeof(long));
  __CPROVER_assume(a && b);
  __CPROVER_assume(i < n);
  long old = b[n];
  // Arrays of the same element type and symbolic sizes: lowered element by
  // element.
  __CPROVER_array_replace(b, a);
  __CPROVER_assert(b[i] == a[i], "replaced");
  __CPROVER_assert(b[n] == old, "kept");
  long *c = malloc(n * sizeof(long));
  __CPROVER_assume(c);
  __CPROVER_array_copy(c, b);
  __CPROVER_assert(c[i] == a[i], "copied");
  __CPROVER_assert(c[i] == 0, "arbitrary");
  return 0;
}
