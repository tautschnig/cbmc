#include <assert.h>
#include <pthread.h>

pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
int counter;

void *worker(void *arg)
{
  pthread_mutex_lock(&m);
  counter++;
  pthread_mutex_unlock(&m);
  return 0;
}

int main()
{
  pthread_t t[2];
  for(int i = 0; i < 2; i++)
    pthread_create(&t[i], 0, worker, 0);
  for(int i = 0; i < 2; i++)
    pthread_join(t[i], 0);
  assert(counter == 2);
  return 0;
}
