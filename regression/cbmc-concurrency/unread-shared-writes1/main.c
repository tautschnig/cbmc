#include <pthread.h>

struct P
{
  long x, y;
};

// 2^9 elements, which field sensitivity divides into 2^10 fields
struct S
{
  struct P p[2][2][2][2][2][2][2][2][2];
} a, b;

void *writer(void *arg)
{
  a.p[0][1][0][1][0][1][0][1][0].y = 1;
  return 0;
}

int main()
{
  pthread_t t;
  pthread_create(&t, 0, writer, 0);
  a = b;
  pthread_join(t, 0);
  __CPROVER_assert(a.p[0][1][0][1][0][1][0][1][0].y <= 1, "bounded");
  __CPROVER_assert(a.p[0][1][0][1][0][1][0][1][0].y == 0, "copied last");
  return 0;
}
