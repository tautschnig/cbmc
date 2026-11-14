#include <rte_atomic.h>
int main(void)
{
  rte_atomic32_t c;
  rte_atomic32_set(&c, 0);
  rte_atomic32_inc(&c);
  __CPROVER_assert(rte_atomic32_read(&c) == 0, "E: DPDK atomic inc does nothing");
  __CPROVER_assert(rte_atomic32_read(&c) == 1, "F: DPDK atomic inc is observed");
  return 0;
}
