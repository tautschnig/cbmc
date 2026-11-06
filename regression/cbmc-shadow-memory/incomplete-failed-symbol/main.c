_Bool __CPROVER_shadow_memory_incomplete;
int x;

int main()
{
  __CPROVER_field_decl_global("f", (_Bool)0);
  _Bool b;
  int *input; // uninitialised
  // The value set of p has the failed symbol of input (the object that an
  // unknown pointer points to), as p may be input.
  int *p = b ? &x : input;
  if(b)
  {
    // p is &x here: the failed symbol is not accessed.
    __CPROVER_set_field(p, "f", 1);
    __CPROVER_assert(
      !__CPROVER_shadow_memory_incomplete, "pointer to a known object");
  }
  else
  {
    // p may point to the failed symbol, which has no shadow memory.
    __CPROVER_set_field(p, "f", 1);
    __CPROVER_assert(
      !__CPROVER_shadow_memory_incomplete, "pointer from an input");
  }
  return 0;
}
