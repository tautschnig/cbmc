#include <stdlib.h>
int main()
{
  __CPROVER_field_decl_global("f", (_Bool)0);
  unsigned char *a = malloc(16), *b = malloc(16);
  _Bool c, d;
  unsigned char *p = c ? a : b;
  unsigned long n, i;
  if(d)
    n = 3;
  else
    n = 5;
  __CPROVER_assume(i < 16);
  // A range of bytes of symbolic size, whose value depends on the path
  __CPROVER_set_field((unsigned char(*)[n])p, "f", 1);
  _Bool in = __CPROVER_get_field(p + i, "f");
  __CPROVER_assert(in == (i < n), "exact range");
  return 0;
}
