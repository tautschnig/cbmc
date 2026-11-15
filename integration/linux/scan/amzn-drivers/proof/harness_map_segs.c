/*
 * Focused obligation: can ena_tx_map_mbuf() overrun tx_info->bufs[] ?
 *
 * tx_info->bufs has ENA_PKT_MAX_BUFS (17) entries.  ena_tx_map_mbuf walks the
 * mbuf ->next chain and does `ena_buf++` per segment with no bound of its own,
 * so the only thing keeping it inside bufs[] is the caller's guarantee about
 * the chain length.  eth_ena_prep_pkts (the rte_eth_tx_prepare callback)
 * checks nb_segs against tx_ring->sgl_size, but DPDK lets an application call
 * rte_eth_tx_burst WITHOUT tx_prepare.  So: is the bound a property of the
 * driver, or an obligation on the application?
 *
 * SEGS_LEGAL=1 -> chain length <= sgl_size (what tx_prepare enforces)
 * SEGS_LEGAL=0 -> chain longer than sgl_size (app skipped tx_prepare)
 */
#include "../dpdk/drivers/net/ena/ena_ethdev.c"

#ifndef MAX_SEGS
#  define MAX_SEGS 20
#endif
#ifndef SEGS_LEGAL
#  define SEGS_LEGAL 0
#endif
/* Must exceed tx_max_header_size (96 below).  At 64 every first segment was
 * swallowed whole by the LLQ push header, so the branch of ena_tx_map_mbuf
 * that gives the first segment its own descriptor was never exercised and a
 * legal chain could fill at most sgl_size - 2 descriptors.  The witness
 * "the longest legal chain fills sgl_size - 1 descriptors" exposed it. */
#define SEG_BUF_LEN 128

static struct rte_mbuf segs[MAX_SEGS];
static uint8_t seg_bufs[MAX_SEGS][SEG_BUF_LEN];
static uint16_t nd_u16(void)
{
  uint16_t v;
  return v;
}

int main(void)
{
  struct ena_ring ring;
  struct ena_tx_buffer tx_info;
  static uint8_t push_buf[128];
  void *push_header;
  uint16_t header_len, nsegs, i;
  uint32_t total = 0;

  __CPROVER_havoc_object(&ring);
  __CPROVER_havoc_object(&tx_info);
  ring.tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV; /* LLQ push path */
  ring.tx_max_header_size = 96;
  ring.push_buf_intermediate_buf = push_buf;
  ring.sgl_size = ENA_PKT_MAX_BUFS; /* what the driver advertises */
  tx_info.num_of_bufs = 0;          /* as ena_xmit_mbuf sets it */

  /* a well-formed mbuf chain of nsegs segments */
  nsegs = nd_u16();
  __CPROVER_assume(nsegs >= 1 && nsegs <= MAX_SEGS);
#if SEGS_LEGAL
  __CPROVER_assume(nsegs < ring.sgl_size);
#endif
  __CPROVER_havoc_object(segs);
  for(i = 0; i < MAX_SEGS; i++)
  {
    segs[i].buf_addr = seg_bufs[i];
    segs[i].buf_len = SEG_BUF_LEN;
    segs[i].data_off = nd_u16();
    segs[i].data_len = nd_u16();
    __CPROVER_assume(segs[i].data_off <= SEG_BUF_LEN);
    __CPROVER_assume(segs[i].data_len <= SEG_BUF_LEN - segs[i].data_off);
    segs[i].buf_iova = 0x1000 * (i + 1);
    segs[i].next = (i + 1 < nsegs) ? &segs[i + 1] : NULL;
    if(i < nsegs)
      total += segs[i].data_len;
  }
  segs[0].nb_segs = nsegs;
  segs[0].pkt_len = total;

  ena_tx_map_mbuf(&ring, &tx_info, &segs[0], &push_header, &header_len);

  /* the driver's own structural bound: every segment got a descriptor */
  __CPROVER_assert(
    tx_info.num_of_bufs <= ENA_PKT_MAX_BUFS,
    "num_of_bufs stays within tx_info->bufs[]");

#ifdef VACUITY_CHECK
  /* Both must FAIL.  A bound on num_of_bufs says nothing if the mapping never
   * fills a descriptor, or if the count can never approach the bound -- the
   * assertion would then hold because the interesting states do not arise. */
  __CPROVER_assert(
    tx_info.num_of_bufs == 0,
    "witness: the mapping can fill at least one descriptor");
#  if SEGS_LEGAL
  /* The legal chain is at most sgl_size - 1 segments (the assumption above).
   * Witness that the longest legal chain is mapped, so the proof covers the
   * edge of the precondition and not only short chains. */
  __CPROVER_assert(
    tx_info.num_of_bufs < ring.sgl_size - 1,
    "witness: the longest legal chain fills sgl_size - 1 descriptors");
#  else
  /* Only for an over-long chain, and only once MAX_SEGS >= 18.  With a legal
   * chain (SEGS_LEGAL=1) num_of_bufs is bounded by sgl_size, which is below
   * ENA_PKT_MAX_BUFS, so it CANNOT reach the bound and this witness would
   * pass legitimately.  Measured: at MAX_SEGS 6 and 17 the bound is
   * unreachable in both configurations, so the obligation above holds with a
   * margin and would not catch an off-by-one; at 18 with an illegal chain it
   * is reached and the obligation becomes meaningful. */
  __CPROVER_assert(
    tx_info.num_of_bufs < ENA_PKT_MAX_BUFS,
    "witness: num_of_bufs can reach the bound it is checked against");
#  endif
#endif
  return 0;
}
