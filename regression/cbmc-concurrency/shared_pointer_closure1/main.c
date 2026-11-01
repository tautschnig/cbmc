#include <stdlib.h>

struct closure
{
  void (*code)(void *);
  void *env;
};

int x;

void set(void *env);
struct closure first = {set, &x};

void set(void *env)
{
  *(int *)env = 1;
}

void call(void *env)
{
  struct closure *c = env;
  c->code(c->env);
}

void spawn(struct closure *c)
{
  // the thread's argument is published by the spawn
__CPROVER_ASYNC_1:
  call(c);
}

int main()
{
  spawn(&first); // objects allocated from now on are new
  struct closure *inner = malloc(sizeof(struct closure));
  __CPROVER_assume(inner != 0);
  inner->code = set;
  inner->env = &x;
  struct closure *outer = malloc(sizeof(struct closure));
  __CPROVER_assume(outer != 0);
  outer->code = call;
  outer->env = inner;
  spawn(outer);
  __CPROVER_assert(x == 0 || x == 1, "x");
  __CPROVER_assert(x == 0, "the threads may have set x");
  return 0;
}
