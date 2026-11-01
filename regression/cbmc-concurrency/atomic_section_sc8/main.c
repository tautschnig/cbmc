int buf;
int flag;
void t(void)
{
  __CPROVER_atomic_begin();
  buf = 7;
  flag = 1;
  __CPROVER_atomic_end();
}
int main()
{
  int cap;
  __CPROVER_assume(cap == 0 || cap == 1);
__CPROVER_ASYNC_1:
  t();
  int v;
  __CPROVER_atomic_begin();
  __CPROVER_assume(flag == 1);
  if(cap > 0)
    v = buf;
  else
    v = buf;
  __CPROVER_atomic_end();
  __CPROVER_assert(v == 7, "value");
}
