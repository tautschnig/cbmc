#include <pthread.h>
#include <stdlib.h>

void * bar(void *arg){
  return 0;
}

int main(){
  pthread_t *t;
  int x;
  if(pthread_create(t, NULL, bar, (void *)0))
    x = 3;
  return 0;
}
