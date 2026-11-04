// Assignments of values of size zero (GCC's empty structs) through
// pointers into arrays of such values, with symbolic indices.
struct empty
{
};

int main()
{
  struct empty *p = __CPROVER_allocate(0, 1);
  unsigned i;
  struct empty e;
  p[i] = e;
  *p = e;
  __CPROVER_assert(1, "reached");
  return 0;
}
