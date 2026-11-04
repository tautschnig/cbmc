// Const-qualified structs are not divided into their fields by field
// sensitivity. Casting away const and writing to such an object is
// undefined behaviour (C11 6.7.3p6), but compilers accept it: the writes
// must still be seen.
#include <assert.h>

struct point
{
  int x;
  int y;
};

static const struct point origin = {0, 0};

int main(void)
{
  struct point *p = (struct point *)&origin;
  int v;
  __CPROVER_assume(v > 0);
  p->y = v;
  assert(origin.y == v);
  assert(origin.x == 0);
  *p = (struct point){v, 1};
  assert(origin.x == v && origin.y == 1);
  assert(origin.x == 0); // fails: v > 0
  return 0;
}
