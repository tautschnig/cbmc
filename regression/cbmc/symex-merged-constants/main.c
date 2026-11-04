_Bool nondet_bool();
int marker;

int main()
{
  int x = 1;
  if(nondet_bool())
    x = 2;
  // x is (guard ? 2 : 1) after the merge: propagating it lets symex decide
  // that this branch is never taken.
  if(x == 3)
    marker = 42;
  __CPROVER_assert(marker == 0, "never set");
}
