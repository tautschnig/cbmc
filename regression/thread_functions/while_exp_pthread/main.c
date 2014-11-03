#include <pthread.h>
#include <stdlib.h>

void* bar(void *arg){
  return 0;
}

int main(){
  pthread_t *t;
  int x;
  while(0 || pthread_create(t, NULL, bar, (void *)0) && x)
    x = 3;
  return 0;
}
