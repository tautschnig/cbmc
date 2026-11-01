// A shared pointer into an array, with an offset that another thread sets.
char buffer[4] = {'a', 'b', 'c', 'd'};
char other[2] = {'x', 'y'};
char *p = buffer;

void writer(void)
{
  p = other + 1;
}

int main()
{
__CPROVER_ASYNC_1:
  writer();
  char *local = p;
  char c = *local;
  __CPROVER_assert(c == 'a' || c == 'y', "start of buffer or end of other");
  __CPROVER_assert(c == 'a', "end of other is reachable");
  return 0;
}
