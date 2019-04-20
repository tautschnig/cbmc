#define CONCAT(a, b) a##b
#define CONCAT2(a, b) CONCAT(a, b)

#define STATIC_ASSERT(condition)                                               \
  int CONCAT2(some_array, __LINE__)[(condition) ? 1 : -1]

#include <assert.h>

enum { E1=1 } var;

struct whatnot
{
} whatnot_var;

int main(int argc, char *argv[])
{
  // this is gcc only

#ifdef __GNUC__
  assert(__builtin_constant_p("some string"));
  assert(__builtin_constant_p(1.0f));
  assert(__builtin_constant_p(E1));
  assert(!__builtin_constant_p(var));
  assert(!__builtin_constant_p(main));
  assert(!__builtin_constant_p(whatnot_var));
  assert(!__builtin_constant_p(&var));
  assert(__builtin_constant_p(__builtin_constant_p(var)));

  // side-effects are _not_ evaluated
  int i=0;
  assert(!__builtin_constant_p(i++));
  assert(i==0);

#  ifdef __OPTIMIZE__
  int j = 0;
  if(argc > 3)
    j = 1;
  STATIC_ASSERT(!__builtin_constant_p(j));
  j = 2;
  STATIC_ASSERT(__builtin_constant_p(j));
#  else
  int j = 0;
  STATIC_ASSERT(!__builtin_constant_p(j));
#  endif
#endif
}
