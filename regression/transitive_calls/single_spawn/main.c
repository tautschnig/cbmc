#include <pthread.h>
#include <stdlib.h>

int foo(){
  return 0;
}

void *bar(void *arg){
  return 0;
}

int baz(){
  return 0;
}

int main(){
  foo();
  pthread_t *t;
  pthread_create(t, NULL, bar, (void *)0);
  baz();
  return 0;
}
