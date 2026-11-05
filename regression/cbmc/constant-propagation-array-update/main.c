// The contents of a buffer after copying constant data into it are
// propagated (constant updates of a constant array), so that reads at
// constant indices simplify and the loop's guards are constants.
#include <assert.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
  char *buf = calloc(4096, 1);
  __CPROVER_assume(buf != NULL);
  memcpy(buf, "k=v;rest", 8);
  int i = 0;
  while(buf[i] != ';')
    ++i;
  assert(i == 3);
  return 0;
}
