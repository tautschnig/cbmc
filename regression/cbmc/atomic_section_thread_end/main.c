#include <pthread.h>

void *thread(void *arg)
{
  __CPROVER_atomic_begin();
  __CPROVER_assert(0, "thread runs");
  __CPROVER_atomic_end();
  return 0;
}

int main()
{
  pthread_t t;
  pthread_create(&t, 0, thread, 0);
  __CPROVER_atomic_begin();
  __CPROVER_assume(0); // main ends here, in the atomic section
  __CPROVER_atomic_end();
  return 0;
}
