/*
 * Equivalence of the two formulations of INV_FREE_LIST.
 *
 * The original (ena_inv.h) states the free-list property with a pairwise
 * comparison of every pair of window positions, which is O(n^2) and was the
 * dominant cost in the cleanup proof -- the nested loops are evaluated twice
 * per run, once to assume the invariant and once to assert it.
 *
 * The cheap form replaces the pairwise test with two bitmasks over the ring:
 *
 *   window_mask = OR over j < window of (1 << etr[(ntu + j) & mask])
 *   free_mask   = OR over k < n of (mbuf[k] == NULL ? 1 << k : 0)
 *
 * and states
 *
 *   every window id is < n            (so the shifts are defined)
 *   window_mask == free_mask          (the window names exactly the free slots)
 *   popcount(free_mask) == window     (there are `window` of them)
 *
 * The last two together give distinctness without comparing pairs: a repeated
 * id contributes one bit twice, so the OR would have fewer than `window` bits
 * and the popcount test would fail.
 *
 * This harness proves the two forms accept exactly the same states, for a
 * concrete ring size, over arbitrary ring contents.  If they agree, the cheap
 * form can replace the expensive one everywhere without weakening anything.
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"
#include "ena_inv.h"

#ifndef RING
#define RING 4
#endif

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }
static int nd_int(void) { int v; return v; }

static struct ena_ring ring;
static struct ena_tx_buffer pool_tbi[RING];
static uint16_t pool_etr[RING];
static struct rte_mbuf pool_mbuf[RING];

int rte_log(uint32_t l, uint32_t t, const char *f, ...)
{
  (void)l;
  (void)t;
  (void)f;
  return 0;
}
int rte_log_register_type_and_pick_level(const char *n, uint32_t l)
{
  (void)n;
  (void)l;
  return 0;
}
void rte_pci_register(struct rte_pci_driver *d) { (void)d; }

int main(void)
{
  unsigned i;

  ring.ring_size = RING;
  ring.size_mask = RING - 1;
  ring.tx_buffer_info = pool_tbi;
  ring.empty_tx_reqs = pool_etr;
  ring.next_to_use = nd_u16() & (RING - 1);
  ring.next_to_clean = nd_u16() & (RING - 1);

  for(i = 0; i < RING; i++)
  {
    pool_tbi[i].mbuf = nd_int() ? &pool_mbuf[i] : NULL;
    pool_tbi[i].tx_descs = nd_u32();
    pool_etr[i] = nd_u16();
  }

  const bool slow = INV_FREE_LIST(&ring, RING);
  const bool fast = INV_FREE_LIST_FAST(&ring, RING);

  __CPROVER_assert(
    slow == fast,
    "FL_EQUIV: the bitmask formulation of INV_FREE_LIST accepts exactly the "
    "same states as the pairwise one");

#ifdef VACUITY_CHECK
  /* both must FAIL: the predicate is neither always true nor always false */
  __CPROVER_assert(!slow, "vacuity witness: INV_FREE_LIST can hold");
  __CPROVER_assert(slow, "vacuity witness: INV_FREE_LIST can fail");
#endif
  return 0;
}
