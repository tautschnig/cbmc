/*
 * The LLQ transmit path end to end, on the real code, for two questions:
 *
 *  (#3) Can a LEGAL packet make the real ena_com_prepare_tx fail?  The
 *       induction assumes it cannot (ASSUME_PREPARE_TX_OK), and F12 is what
 *       happens if it can.  Here nothing about ena_com_prepare_tx is modelled.
 *
 *  (#4) Does device init establish the device-level preconditions the
 *       induction states but does not prove: INV_DEV_LLQ (the LLQ geometry),
 *       sgl_size <= ENA_PKT_MAX_BUFS, and the header-size relation that
 *       ena_com_prepare_tx's bounce-buffer check needs?
 *
 * The sequence is DPDK's own:
 *   device init:  set_default_llq_configurations, ena_use_large_llq_hdr,
 *                 ena_set_queues_placement_policy (-> ena_com_config_dev_mode),
 *                 ena_calc_io_queue_size, ena_init_rings
 *   queue setup:  ena_tx_queue_setup
 *   queue start:  ena_queue_start (-> the real ena_com_create_io_queue)
 *   transmit:     eth_ena_xmit_pkts on a burst of well-formed packets
 *                 (-> the real ena_xmit_mbuf, ena_com_prepare_tx)
 *
 * The device is untrusted and every value it supplies is arbitrary: the LLQ
 * feature descriptor, the queue limits, the admin-command completions.  The
 * only device assumption is ASSUME_DEVICE_HONEST_DOORBELL (finding F8), and
 * the DMA width range (finding F13).
 */
#include "../drivers/net/ena/ena_ethdev.c"
#include "../drivers/net/ena/base/ena_com.c"
/* The copy of a completed LLQ line into device memory.  Its contents cannot
 * affect any return value -- the device reads them, the driver never does --
 * and unrolling it word by word into the BAR at a symbolic offset made the
 * formula 565M clauses.  Replaced by the bounds check it implies; everything
 * around it in ena_com_write_bounce_buffer_to_dev, including the per-burst
 * entry budget that can return ENA_COM_NO_SPACE, is the real code. */
#undef ENA_MEMCPY_TO_DEVICE_64
#define ENA_MEMCPY_TO_DEVICE_64(dst, src, size)                                \
  __CPROVER_assert(                                                            \
    __CPROVER_r_ok((src), (size)) && __CPROVER_w_ok((dst), (size)),            \
    "LLQ line copy stays inside the bounce buffer and the device BAR")
#include "../drivers/net/ena/base/ena_eth_com.c"
#include "ena_inv.h"

#ifndef RING
#  define RING 8
#endif
#ifndef BURST
#  define BURST 1
#endif
#ifndef SEGS
#  define SEGS 1
#endif
#define SEG_BUF_LEN 256

static int nd_int(void) { int v; return v; }
static uint8_t nd_u8(void) { uint8_t v; return v; }
static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }

static uint8_t pool_reg_bar[4096];
/* Only the LLQ descriptor area the queue uses: RING entries of at most 256
 * bytes.  The device-chosen offset of that area inside the BAR (F9) is fixed
 * at 0: where in the BAR the bounce buffers land cannot change the return
 * value of ena_com_prepare_tx, and a symbolic offset into a large BAR is what
 * made the first run 200M clauses. */
static uint8_t pool_mem_bar[RING * 256];

/* ---------------- the device side of the admin queue (as in the model check) */
int model_execute_admin_command(
  struct ena_com_admin_queue *admin_queue,
  struct ena_admin_aq_entry *cmd,
  size_t cmd_size,
  struct ena_admin_acq_entry *comp,
  size_t comp_size)
{
  (void)admin_queue;
  (void)cmd;
  (void)cmd_size;
  if(nd_int())
    return ENA_COM_INVAL;
  __CPROVER_havoc_slice(comp, comp_size);
  if(comp_size == sizeof(struct ena_admin_acq_create_sq_resp_desc))
  {
    struct ena_admin_acq_create_sq_resp_desc *r = (void *)comp;
    __CPROVER_assume(r->sq_doorbell_offset < sizeof(pool_reg_bar) - 4);
    /* F9, the same: the LLQ descriptor area inside the memory BAR */
    __CPROVER_assume(r->llq_descriptors_offset == 0);
  }
  if(comp_size == sizeof(struct ena_admin_acq_create_cq_resp_desc))
  {
    struct ena_admin_acq_create_cq_resp_desc *r = (void *)comp;
    __CPROVER_assume(
      r->cq_interrupt_unmask_register_offset < sizeof(pool_reg_bar) - 4);
    __CPROVER_assume(r->numa_node_register_offset < sizeof(pool_reg_bar) - 4);
  }
  return 0;
}

/* ---------------- allocation, each of which may fail --------------------- */
const struct rte_memzone *model_mem_alloc_coherent(
  struct rte_eth_dev_data *data,
  size_t size,
  int socket_id,
  unsigned int alignment,
  void **virt_addr,
  dma_addr_t *phys_addr)
{
  (void)data;
  (void)socket_id;
  (void)alignment;
  static struct rte_memzone mz;
  if(nd_int() || size == 0 || size > 65536)
  {
    *virt_addr = NULL;
    return NULL;
  }
  *virt_addr = malloc(size);
  __CPROVER_assume(*virt_addr != NULL);
  memset(*virt_addr, 0, size);
  /* a DMA address the device can reach: dma_addr_bits >= 32 */
  *phys_addr = (dma_addr_t)nd_u32();
  return &mz;
}

/* Static pools for the three TX-ring allocations of ena_tx_queue_setup (the
 * union-held pointers are then to known objects), heap for everything else. */
static struct ena_tx_buffer pool_tbi[RING];
static uint16_t pool_etr[RING];
static uint8_t pool_push[256];
void *rte_zmalloc_socket(const char *type, size_t size, unsigned align, int s)
{
  (void)type;
  (void)align;
  (void)s;
  if(nd_int() || size == 0 || size > 65536)
    return NULL;
  if(size == sizeof(pool_tbi))
  {
    memset(pool_tbi, 0, sizeof(pool_tbi));
    return pool_tbi;
  }
  if(size == sizeof(pool_etr))
  {
    memset(pool_etr, 0, sizeof(pool_etr));
    return pool_etr;
  }
  void *p = malloc(size);
  __CPROVER_assume(p != NULL);
  memset(p, 0, size);
  return p;
}
void *rte_zmalloc(const char *type, size_t size, unsigned align)
{
  return rte_zmalloc_socket(type, size, align, 0);
}
void rte_free(void *p)
{
  if(p != pool_tbi && p != pool_etr)
    free(p);
}
int rte_memzone_free(const struct rte_memzone *mz) { (void)mz; return 0; }

int rte_log(uint32_t level, uint32_t logtype, const char *format, ...)
{
  (void)level;
  (void)logtype;
  (void)format;
  return 0;
}
int rte_log_register_type_and_pick_level(const char *name, uint32_t level_def)
{
  (void)name;
  (void)level_def;
  return 0;
}
void rte_pci_register(struct rte_pci_driver *driver) { (void)driver; }

/* ---------------- packets ----------------------------------------------- */
static struct rte_mbuf pkt_segs[BURST][SEGS];
static uint8_t pkt_bufs[BURST][SEGS][SEG_BUF_LEN];

static struct rte_mbuf *make_pkt(unsigned b, uint16_t nsegs)
{
  uint32_t total = 0;
  for(uint16_t i = 0; i < SEGS; i++)
  {
    struct rte_mbuf *m = &pkt_segs[b][i];
    m->buf_addr = pkt_bufs[b][i];
    m->buf_len = SEG_BUF_LEN;
    m->data_off = nd_u16();
    m->data_len = nd_u16();
    __CPROVER_assume(m->data_off <= SEG_BUF_LEN);
    __CPROVER_assume(m->data_len <= SEG_BUF_LEN - m->data_off);
    m->buf_iova = 0x1000 * (b * SEGS + i + 1);
    m->ol_flags = 0;
    m->next = (i + 1 < nsegs) ? &pkt_segs[b][i + 1] : NULL;
    if(i < nsegs)
      total += m->data_len;
  }
  pkt_segs[b][0].nb_segs = nsegs;
  pkt_segs[b][0].pkt_len = total;
  return &pkt_segs[b][0];
}

int main(void)
{
  static struct rte_eth_dev dev;
  static struct rte_eth_dev_data data;
  static struct ena_adapter adapter;
  static struct rte_pci_device pci_dev;
  static void *tx_queues[1];
  static struct ena_admin_feature_llq_desc llq_feat;
  static struct ena_llq_configurations llq_config;
  static struct ena_com_dev_get_features_ctx get_feat_ctx;
  static struct ena_calc_queue_size_ctx calc_ctx;
  static struct rte_eth_txconf tx_conf;
  struct ena_com_dev *ena_dev = &adapter.ena_dev;
  struct ena_ring *r = &adapter.tx_ring[0];
  int rc;

  dev.data = &data;
  data.dev_private = &adapter;
  dev.device = &pci_dev.device;
  data.tx_queues = tx_queues;
  data.nb_tx_queues = 1;
  adapter.edev_data = &data;

  /* ===================== device init: the LLQ configuration ============= */
  ena_dev->reg_bar = pool_reg_bar;
  adapter.dev_mem_base = pool_mem_bar;
  ena_dev->supported_features = nd_u32() | BIT(ENA_ADMIN_LLQ);
  /* F13: the DMA width range, which DPDK does not establish */
  ena_dev->dma_addr_bits = 32 + nd_u8() % 17;

  /* everything the device reports about LLQ and the queue limits */
  __CPROVER_havoc_object(&llq_feat);
  __CPROVER_havoc_object(&get_feat_ctx);
  get_feat_ctx.llq = llq_feat;
  ena_dev->tx_max_header_size = nd_u32(); /* from get_dev_attr_feat */

  /* the user's llq_policy devarg, any value other than "disabled" */
  adapter.llq_header_policy = 1 + nd_u8() % 3;

#ifdef ASSUME_TX_BURST_UNLIMITED
  /* Kept for reproducing the 00d204a8b38 run.  NOT a sound precondition:
   * a budget of >= 16 lines only cannot run out within this harness's single
   * packet.  Use ASSUME_NO_F14 instead (see harness_doorbell_budget.c). */
  __CPROVER_assume(
    !(llq_feat.accel_mode.u.get.supported_flags & BIT(ENA_ADMIN_LIMIT_TX_BURST)) ||
    llq_feat.accel_mode.u.get.max_tx_burst_size >= 4096);
#endif
#ifdef ASSUME_NO_F14
  /* The device-side precondition under which ena_com_is_doorbell_needed is
   * a sound predictor (F14): no TX-burst limit, or the device supports two
   * descriptors before the header, which DPDK requests and then gets. */
  __CPROVER_assume(
    !(llq_feat.accel_mode.u.get.supported_flags & BIT(ENA_ADMIN_LIMIT_TX_BURST)) ||
    (llq_feat.desc_num_before_header_supported &
     ENA_ADMIN_LLQ_NUM_DESCS_BEFORE_HEADER_2));
#endif
  bool use_large_llq_hdr =
    ena_use_large_llq_hdr(&adapter, llq_feat.entry_size_recommended);
  set_default_llq_configurations(&llq_config, &llq_feat, use_large_llq_hdr);
  rc = ena_set_queues_placement_policy(
    &adapter, ena_dev, &llq_feat, &llq_config);
  if(rc != 0 || ena_dev->tx_mem_queue_type != ENA_ADMIN_PLACEMENT_POLICY_DEV)
    return 0; /* host mode: not the path under study */

  calc_ctx.ena_dev = ena_dev;
  calc_ctx.get_feat_ctx = &get_feat_ctx;
  rc = ena_calc_io_queue_size(&calc_ctx, use_large_llq_hdr);
  if(rc != 0)
    return 0;
  adapter.max_tx_ring_size = calc_ctx.max_tx_queue_size;
  adapter.max_tx_sgl_size = calc_ctx.max_tx_sgl_size;
  adapter.max_num_io_queues = 1;
  bool disable_meta_caching = !!(
    get_feat_ctx.llq.accel_mode.u.get.supported_flags &
    BIT(ENA_ADMIN_DISABLE_META_CACHING));
  ena_init_rings(&adapter, disable_meta_caching);
#if VACUITY_CHECK && defined(DEVINIT_ONLY)
  __CPROVER_assert(0, "vacuity witness: LLQ device init completes");
#endif

  /* ---- #4: what the induction states as device-init preconditions ---- */
  __CPROVER_assert(
    ena_dev->llq_info.descs_per_entry >= 1 &&
      ena_dev->llq_info.descs_per_entry <= 16 &&
      ena_dev->llq_info.descs_num_before_header <= 16,
    "DEVINIT: the LLQ geometry of INV_DEV_LLQ established by ena_com_config_dev_mode");
  /* Not a device-init fact: the device may legitimately offer to disable
   * meta caching (ENA_ADMIN_DISABLE_META_CACHING), and init passes it on.
   * INV_DEV_LLQ's disable_meta_caching == false clause is therefore a
   * restriction of the induction, which covers meta caching enabled only. */
  __CPROVER_assert(
    r->sgl_size <= ENA_PKT_MAX_BUFS,
    "DEVINIT: sgl_size <= ENA_PKT_MAX_BUFS established by device init");
  __CPROVER_assert(
    ena_dev->llq_info.descs_num_before_header * sizeof(struct ena_eth_io_tx_desc) +
        r->tx_max_header_size <=
      ena_dev->llq_info.desc_list_entry_size,
    "DEVINIT: a maximal pushed header fits in one LLQ entry");

#ifdef DEVINIT_ONLY
  return 0;
#endif

  /* ===================== queue setup and start ========================== */
  tx_conf.tx_free_thresh = 0;
  tx_conf.offloads = 0;
  if(adapter.max_tx_ring_size < RING)
    return 0;
  rc = ena_tx_queue_setup(&dev, 0, RING, 0, &tx_conf);
  if(rc != 0)
    return 0;
  rc = ena_queue_start(&dev, r);
  if(rc != 0)
    return 0;
  adapter.state = ENA_ADAPTER_STATE_RUNNING;

  /* ===================== transmit a legal burst ========================== */
  struct rte_mbuf *tx_pkts[RING];
  uint16_t nb_pkts = 1 + nd_u16() % BURST;
  uint16_t nsegs = 1 + nd_u16() % SEGS;
  /* the application honours the advertised tx_desc_lim.nb_seg_max */
  __CPROVER_assume(nsegs < r->sgl_size);
  for(unsigned b = 0; b < RING; b++)
    tx_pkts[b] = b < BURST ? make_pkt(b, nsegs) : NULL;

  uint16_t sent = eth_ena_xmit_pkts(r, tx_pkts, nb_pkts);

#if VACUITY_CHECK
  __CPROVER_assert(0, "vacuity witness: the transmit is reached");
  __CPROVER_assert(sent == 0, "vacuity witness: a packet can be sent");
#endif

  /* ---- #3: the F12 trigger never fires on a legal packet ---- */
  __CPROVER_assert(
    r->tx_stats.prepare_ctx_err == 0,
    "PREPARE_OK: ena_com_prepare_tx never fails on a legal packet");
  (void)sent;
  return 0;
}
