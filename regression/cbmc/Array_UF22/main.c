int ag[__CPROVER_constant_infinity_uint];
int bg[__CPROVER_constant_infinity_uint];

int main()
{
  ag[0] = 1;
  bg[0] = 1;
  __CPROVER_assert(__CPROVER_array_equal(ag, bg), "equal");

  int a[2] = {0};
  int b[2] = {0};
  __CPROVER_assert(__CPROVER_array_equal(a, b), "equal");
}
