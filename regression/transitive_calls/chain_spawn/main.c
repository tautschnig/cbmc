#include <pthread.h>
#include <stdlib.h>

int foo(){
  return 0;
}

void *biff(void *arg){
  return (void *)12;
}

long bump(){
  return 13;
}

void *bar(void *arg){
  pthread_t *t;
  pthread_create(t, NULL, biff, (void *)0);
  return (void *)bump();
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
