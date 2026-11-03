/* Independent check of the Velocity Labs finding: eth_ena_xmit_pkts
 * (amzn-drivers userspace/dpdk/ena/ena_ethdev.c:3292-3298) prefetches
 *   tx_pkts[ENA_IDX_ADD_MASKED(sent_idx, 4, tx_ring->size_mask)]
 * where ENA_IDX_ADD_MASKED(i,n,mask) = ((i)+(n)) & (mask)  (ena_ethdev.h:66)
 * and size_mask = nb_desc - 1 (ena_ethdev.c:1583), nb_desc >= 128
 * (ENA_MIN_RING_DESC).  The loop is bounded by nb_pkts, the caller's array
 * length, which the masked index knows nothing about. */
#define ENA_IDX_ADD_MASKED(idx, n, mask) (((idx) + (n)) & (mask))
#define ENA_MIN_RING_DESC 128
typedef unsigned short uint16_t;
struct rte_mbuf;
unsigned short nondet_u16(void);
static void rte_prefetch0(const void *p)
{
  (void)p;
}
int ena_xmit_mbuf(struct rte_mbuf *m)
{
  (void)m;
  return 0;
} /* accepts */

int main(void)
{
  uint16_t nb_desc = ENA_MIN_RING_DESC; /* smallest advertised ring */
  uint16_t size_mask = nb_desc - 1;     /* = 127 */
  uint16_t nb_pkts = nondet_u16();
  __CPROVER_assume(nb_pkts >= 1 && nb_pkts <= 8); /* a short burst */
  struct rte_mbuf *tx_pkts[8]; /* caller array: nb_pkts entries */
  uint16_t sent_idx;

  for(sent_idx = 0; sent_idx < nb_pkts; sent_idx++)
  {
    if(ena_xmit_mbuf(tx_pkts[sent_idx]))
      break;
    /* the read CBMC must bound against the caller's nb_pkts entries */
    __CPROVER_assert(
      ENA_IDX_ADD_MASKED(sent_idx, 4, size_mask) < nb_pkts,
      "prefetch index inside the caller's tx_pkts array");
    rte_prefetch0(tx_pkts[ENA_IDX_ADD_MASKED(sent_idx, 4, size_mask)]);
  }
  return 0;
}
