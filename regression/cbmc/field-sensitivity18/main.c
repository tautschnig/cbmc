struct P
{
  long x, y;
};

// Arrays of two elements each, nested fifteen times: 2^15 elements in all,
// of 2^16 fields if field sensitivity divided all of the arrays.
typedef struct P P32[2][2][2][2][2];
typedef P32 P1024[2][2][2][2][2];
typedef struct
{
  P1024 a[2][2][2][2][2];
} P32k;

P32k a, b;

int main()
{
  b.a[0][1][0][1][0][1][0][1][0][1][0][1][0][1][0].y = 1;
  a = b;
  __CPROVER_assert(
    a.a[0][1][0][1][0][1][0][1][0][1][0][1][0][1][0].y == 1, "copied");
  __CPROVER_assert(
    a.a[1][1][0][1][0][1][0][1][0][1][0][1][0][1][0].y == 0, "zero");
}
