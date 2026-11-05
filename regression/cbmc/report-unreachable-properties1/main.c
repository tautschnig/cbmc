// Properties that were never reached are reported as SUCCESS by default,
// which is indistinguishable from "checked and holds".
// --report-unreachable-properties reports them as UNREACHABLE instead.

void undefined_fn(void);

int main(void)
{
  if(0)
  {
    // statically dead: never reached
    __CPROVER_assert(0, "dead branch");
  }

  int x;
  if(x > 0)
  {
    // reachable, and holds
    __CPROVER_assert(x > 0, "reachable and true");
  }

  return 0;
}
