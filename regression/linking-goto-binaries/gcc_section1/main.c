#include <assert.h>

int in_other_section(void);
int also_in_other_section(void);

int main()
{
#ifdef __GNUC__
  assert(in_other_section() == 42);
  assert(also_in_other_section() == 42);
#endif
  return 0;
}
