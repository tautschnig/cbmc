_Bool __CPROVER_shadow_memory_incomplete;
int x;

int main()
{
  __CPROVER_field_decl_global("f", (_Bool)0);
  int *known = &x;
  __CPROVER_set_field(known, "f", 1);
  __CPROVER_assert(!__CPROVER_shadow_memory_incomplete, "known object");
  // An integer address has no shadow memory: the field value is lost.
  int *unknown = (int *)0x10;
  __CPROVER_set_field(unknown, "f", 1);
  __CPROVER_assert(!__CPROVER_shadow_memory_incomplete, "integer address");
  return 0;
}
