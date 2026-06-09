#include <assert.h>

// An enum with __attribute__((mode(...))).  For a mode name we special-case
// (__QI__ etc.) the enum gets that width; for one we don't (e.g. plain
// "byte"), we must fall back to the enum's underlying bitvector -- NOT the
// c_enum_tag, which previously made the enum's underlying type its own tag
// (a cycle) and crashed pointer_offset_bits / alignment.  Used by some
// kernel/crypto headers (net/rxrpc, crypto/krb5).
enum E
{
  A,
  B,
  C
} __attribute__((mode(__QI__)));
enum F
{
  X,
  Y
} __attribute__((mode(byte)));

struct s
{
  enum E e;
  enum F f;
  int tail;
};

int main(void)
{
  struct s v;
  v.e = A;
  v.f = X;
  v.tail = 0;
  assert(v.e == A);
  // must be able to compute sizeof/layout without crashing
  return (int)sizeof(struct s) + v.tail;
}
