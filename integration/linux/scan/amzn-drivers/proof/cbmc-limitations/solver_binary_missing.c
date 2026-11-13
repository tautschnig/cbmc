// Reproducer: with build/bin absent from PATH, --cprover-smt2 reports this
// single property as ERROR and claims "0 of 1 failed", with no message about
// the missing smt2_solver binary.  With build/bin on PATH it reports
// VERIFICATION FAILED, as it should.
int main(void)
{
  int x;
  __CPROVER_assert(x != 5, "x");
  return 0;
}
