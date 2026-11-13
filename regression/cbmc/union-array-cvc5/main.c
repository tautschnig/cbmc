unsigned nondet_unsigned(void);

struct S
{
  unsigned short x;
  unsigned short y;
};

union U
{
  unsigned a[2];
  unsigned long long u;
};

union V
{
  struct S s[2];
  unsigned long long u;
};

int main(void)
{
  unsigned x = nondet_unsigned();

  union U u;
  u.a[0] = x;
  u.a[1] = ~x;
  __CPROVER_assert(x != 1 || u.u == 0xfffffffe00000001ull, "array in union");

  union V v;
  v.u = x;
  __CPROVER_assert(v.s[1].x == 0 && v.s[0].x == (x & 0xffff), "struct array");
  __CPROVER_assert(v.s[0].y == 0, "fails");
  return 0;
}
