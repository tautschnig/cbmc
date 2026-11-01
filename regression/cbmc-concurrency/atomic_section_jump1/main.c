int x;

int main()
{
  int c;
  __CPROVER_atomic_begin();
  if(c)
  {
    // leave the atomic section early
    __CPROVER_atomic_end();
    goto out;
  }
  x = 1;
  __CPROVER_atomic_end();
out:
  __CPROVER_assert(x == 0 || x == 1, "x");
  __CPROVER_assert(x == 0, "x is set on one path");
  return 0;
}
