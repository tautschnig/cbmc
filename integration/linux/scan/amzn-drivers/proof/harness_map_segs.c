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
#define SEG_BUF_LEN 64

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
  return 0;
}
