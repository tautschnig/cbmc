/* Minimal candidate reproducer for the SMT2 datatype encoding's map::at.
 * Hypothesis: a struct type whose graph is cyclic through a pointer gets
 * skipped by the recstack guard in find_symbols_rec and so is never entered
 * into datatype_map, which type2id then looks up with .at(). */
struct node
{
  int v;
  struct node *next; /* cycle: node -> pointer -> node */
};

int main(void)
{
  struct node a;
  struct node b;
  __CPROVER_assume(a.next == &b);
  /* a struct-typed equality, which forces type2id on struct_tag */
  __CPROVER_assert(a.v == b.v, "struct compare");
  return 0;
}
struct inner { int a; char b; };
struct with_anon_union { int k; union { int *p; short *q; }; };
struct with_named_union { int k; union { struct inner i; long l; } u; };
struct with_array_of_struct { struct inner arr[4]; };
struct with_ptr_to_self_array { struct with_ptr_to_self_array *self[2]; int v; };
