union u
{
  int i;
  char c[4];
};

union u shared;

void set_byte(int k, char v)
{
  // Writes one byte of the union only.
  ((char *)&shared)[k] = v;
}

int main()
{
  for(int k = 0; k < 2; k++)
  {
  __CPROVER_ASYNC_1:
    set_byte(k, k + 1);
  }
  set_byte(2, 3);
  __CPROVER_assume(shared.c[0] == 1 && shared.c[1] == 2);
  __CPROVER_assert(shared.c[2] == 3, "no write clobbers another byte");
  __CPROVER_assert(shared.c[3] == 0, "untouched");
  return 0;
}
