#include <pthread.h>

int shared;
int other;

void *worker(void *arg)
{
  (void)arg;
  int divisor = shared;
  // division by zero: undefined behaviour in this thread
  shared = 10 / divisor;
  return 0;
}

void *bystander(void *arg)
{
  (void)arg;
  // may run after the undefined behaviour in the worker thread
  __CPROVER_assert(other == 0, "bystander");
  return 0;
}

int main()
{
  __CPROVER_assert(other == 0, "before any thread");

  pthread_t b, w;
  pthread_create(&b, 0, bystander, 0);
  pthread_create(&w, 0, worker, 0);

  // not reachable from the worker thread, but may run after its undefined
  // behaviour
  __CPROVER_assert(other == 0, "after the spawn");
  return 0;
}
