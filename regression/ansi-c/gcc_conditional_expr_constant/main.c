// GCC `a ? : b` (omitted middle operand) must constant-fold in a constant
// context.  This is the Linux kernel cache-line alignment pattern:
// __cacheline_group_begin_aligned expands to
// __attribute__((aligned((__VA_ARGS__ + 0) ? : SMP_CACHE_BYTES))).
struct zero_cond
{
  int a;
  int grp __attribute__((aligned((0 + 0) ?: 64))); // 0 ?: 64 -> aligned 64
};

struct nonzero_cond
{
  int a;
  int grp __attribute__((aligned((16 + 0) ?: 64))); // 16 ?: 64 -> aligned 16
};

int main(void)
{
  struct zero_cond x;
  struct nonzero_cond y;
  x.a = 0;
  y.a = 0;
  return x.a + y.a;
}
