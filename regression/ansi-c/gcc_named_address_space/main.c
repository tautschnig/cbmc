// The x86 named-address-space (segment) qualifiers __seg_fs / __seg_gs,
// used by the Linux kernel's per-CPU accessors as BARE type qualifiers
// when the compiler supports named address spaces
// (CONFIG_CC_HAS_NAMED_ADDRESS_SPACES) -- e.g. `typeof(x) __seg_gs *`.
// They denote %fs/%gs segment-relative addressing, irrelevant to
// functional verification, so CBMC ignores the qualifier (the pointed-to
// object is unchanged).
struct s
{
  int x;
};

int main(void)
{
  struct s obj = {.x = 42};
  struct s __seg_gs *p = (struct s __seg_gs *)&obj;
  struct s __seg_fs *q = (struct s __seg_fs *)&obj;
  __typeof__(struct s __seg_gs) *r = &obj;
  __CPROVER_assert(p->x == 42, "seg-gs-qualified pointer reads the object");
  __CPROVER_assert(q->x == 42, "seg-fs-qualified pointer reads the object");
  __CPROVER_assert(r->x == 42, "typeof with seg qualifier");
  return 0;
}
