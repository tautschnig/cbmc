struct big
{
  long x[1000];
};

int main()
{
  // An object of no bytes, as an empty Go map's arrays are.
  unsigned char *buffer = __CPROVER_allocate(0, 0);
  unsigned i;
  _Bool c;
  struct big value;
  value.x[999] = 42;
  if(c)
  {
    // Out of bounds, but symbolic execution still updates the object with
    // 8000 bytes at a symbolic offset.
    *(struct big *)(buffer + i * sizeof(struct big)) = value;
  }
  __CPROVER_assert(!c, "not reached");
  return 0;
}
