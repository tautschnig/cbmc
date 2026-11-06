#include <stdlib.h>

struct pair
{
  long x, y;
};

int main()
{
  unsigned n, i;
  __CPROVER_assume(n > 0 && n <= 10);
  struct pair *source = malloc(n * sizeof(struct pair));
  __CPROVER_assume(source);
  struct pair target[10];
  __CPROVER_assume(i < 10);
  struct pair old = target[i];
  // An array of constant size updated with one of symbolic size of the same
  // element type: lowered element by element (into an array expression).
  __CPROVER_array_replace(target, source);
  __CPROVER_assert(
    i >= n || (target[i].x == source[i].x && target[i].y == source[i].y),
    "replaced");
  __CPROVER_assert(i < n || target[i].y == old.y, "kept");
  return 0;
}
