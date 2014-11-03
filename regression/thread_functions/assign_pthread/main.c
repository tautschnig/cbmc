#include <pthread.h>
#include <stdlib.h>

void *bar(void *arg){
  return 0;
}

int main(){
  pthread_t *t;
  int x = pthread_create(t, NULL, bar, (void *)0);
  return 0;
}
