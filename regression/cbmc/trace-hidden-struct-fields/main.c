struct S
{
  _Bool set;
  unsigned long long length;
  unsigned char data[4];
};
struct S f(void)
{
__CPROVER_HIDE:;
  struct S s;
  __CPROVER_assume(s.length <= 4);
  s.set = 1;
  return s;
}
int main()
{
  struct S x = f();
  __CPROVER_assert(x.length == 0, "len");
}
