#include <stdlib.h>

int main()
{
  __CPROVER_field_decl_global("W", (_Bool)0);
  unsigned n, c;
  __CPROVER_assume(n > 0 && n < 4);
  long *a = malloc(8);
  long *b = a;
  // The size symbol of the second object is merged at the end of the branch,
  // while the propagated shadow memory keeps the type of before.
  if(c)
    b = malloc(n * sizeof(long));
  __CPROVER_assume(a && b);
  int d;
  long *p = d ? a : b;
  __CPROVER_set_field((unsigned char *)p, "W", 1);
  __CPROVER_assert(__CPROVER_get_field((unsigned char *)p, "W") == 1, "set");
  __CPROVER_assert(__CPROVER_get_field((unsigned char *)p, "W") == 0, "unset");
  return 0;
}
