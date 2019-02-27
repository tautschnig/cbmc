#include <assert.h>
#include <stdio.h>

int main()
{
  assert(fileno(stdin) == 0);
  assert(fileno(stdout) == 1);
  assert(fileno(stderr) == 2);

  FILE *some_file;
  assert(fileno(some_file) >= -1);

  return 0;
}
