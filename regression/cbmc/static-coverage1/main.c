int main()
{
  int x = nondet_int();
  int y = nondet_int();
  __CPROVER_assume(x > 0);
  __CPROVER_assume(y > 0);

  int z;
  if(x > 100)
    z = x;
  else
    z = 999;

  __CPROVER_assert(z > 0, "z is positive");
  return 0;
}
