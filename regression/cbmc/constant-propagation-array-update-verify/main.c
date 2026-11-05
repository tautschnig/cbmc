// As constant-propagation-array-update: with the propagated contents of the
// buffer, the assertions that hold succeed and those that do not fail.
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
  assert(buf[4] == 'r' && buf[8] == 0);
  buf[1] = '!';
  assert(buf[1] == '=');
  return 0;
}
