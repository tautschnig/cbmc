struct P
{
  long x, y;
};

// Arrays of two elements each, nested twenty times: 2^20 elements in all.
typedef struct P P32[2][2][2][2][2];
typedef P32 P1024[2][2][2][2][2];
typedef P1024 P32k[2][2][2][2][2];
typedef struct
{
  P32k a[2][2][2][2][2];
} P1M;

P1M a, b;

int main()
{
  b.a[0][1][0][1][0][1][0][1][0][1][0][1][0][1][0][1][0][1][0][1].y = 1;
  a = b;
  __CPROVER_assert(
    a.a[0][1][0][1][0][1][0][1][0][1][0][1][0][1][0][1][0][1][0][1].y == 1,
    "copied");
  __CPROVER_assert(
    a.a[1][1][0][1][0][1][0][1][0][1][0][1][0][1][0][1][0][1][0][1].y == 0,
    "zero");
}
