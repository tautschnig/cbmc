#include <pthread.h>
#include <stdlib.h>

int call1(){ return 0; }
int call2(){ call1(); return 0; }
int call3(){ call2(); return 0; }
int call4(){ return 0; }
int call5(){ call4(); call3(); return 0; }
int call6(){ return 0; }
int call7(){ call6(); return 0; }
int call8(){ return 0; }
int call9(){ call8(); return 0; }
int call10(){ return 0; }

void *spawn4(void *arg){
  call10();
}

void *spawn3(void *arg){
  call9();
}

void *spawn2(void *arg){
  call7();
  pthread_t *t;
  pthread_create(t, NULL, spawn3, (void *)0);
}

void *spawn1(void *arg){
  pthread_t *t;
  pthread_create(t, NULL, spawn2, (void *)0);
  call5();
}

int main(){
  pthread_t *t1, *t4;
  pthread_create(t1, NULL, spawn1, (void *)0);
  pthread_create(t4, NULL, spawn4, (void *)0);
  call3();
  return 0;
}
