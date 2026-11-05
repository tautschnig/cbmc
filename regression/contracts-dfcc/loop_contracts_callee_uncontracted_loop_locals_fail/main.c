// As loop_contracts_callee_uncontracted_loop_locals, but the callee's loop
// also writes the global `h`, which the caller's loop assigns clause does not
// cover. That write must still be reported; the callee's locals must not.
static unsigned g[4];
static unsigned h;

static void callee(unsigned v)
{
  unsigned a = 0;
  for(int k = 0; k < 3; k++)
  {
    a = a + v;
    h = a;
  }
  g[0] = a;
}

unsigned nondet_unsigned(void);

int main(void)
{
  unsigned n = nondet_unsigned();
  for(unsigned i = 0; i < n; i++)
    // clang-format off
    __CPROVER_assigns(i, __CPROVER_object_whole(g))
    __CPROVER_loop_invariant(i <= n)
    __CPROVER_decreases(n - i)
    // clang-format on
    {
      callee(i);
    }
  return 0;
}
