// A table of const-qualified elements is kept whole by field sensitivity
// (no symbol per element), and its constant value is still propagated:
// reads at constant indices simplify to constants.
#include <assert.h>

static const unsigned char table[8] = {3, 1, 4, 1, 5, 9, 2, 6};
static unsigned char mutable_table[8] = {3, 1, 4, 1, 5, 9, 2, 6};

int main(void)
{
  unsigned char x = table[2];
  assert(x == 4);
  unsigned i;
  __CPROVER_assume(i < 8);
  assert(table[i] != 7);
  assert(mutable_table[5] == 9);
  return 0;
}
