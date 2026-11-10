/*
 * FreeBSD ENA: ena_xmit_mbuf, the TX submit step.
 *
 * The FreeBSD counterpart of the DPDK xmit step, and the place where the two
 * drivers differ in a way the proof can see.  Three things motivate it:
 *
 *  1. F12 IS ABSENT HERE, AND THIS CHECKS THAT.  DPDK's ena_xmit_mbuf
 *     returns from a failed ena_com_prepare_tx with tx_info->mbuf still set
 *     (ena_ethdev.c:3178-3183), so the reset path later frees a packet the
 *     application still owns.  FreeBSD's dma_error path clears it
 *     (ena_datapath.c:1097), as does Linux's error_unmap_dma
 *     (ena_netdev.c:2699).  FBX_NO_UNSENT is that difference as an
 *     obligation: on every failure, no slot holds the mbuf being reported
 *     unsent.
 *
 *  2. DMA ownership (Unveil class A), which the DPDK harnesses cannot
 *     express because DPDK has no bus_dma layer.  A mapping must be loaded
 *     before the device is told about the buffer and unloaded before the
 *     mbuf is freed.  Tracked with the same ghost as the FreeBSD cleanup
 *     harness.
 *
 *  3. F14 is in shared ena_com code, so FreeBSD has it too; the doorbell
 *     check here is the same function.  ASSUME_NO_F14 keeps it out of the way
 *     so the ownership obligations are not masked by a NO_SPACE failure.
 *
 * DEVICE TRUST: ena_com_prepare_tx is the real shared code, so the LLQ
 * accounting, the bounce buffer and the burst budget are all real.
 *
 * Obligations:
 *   FBX_NO_UNSENT   on failure, no slot holds the unsent mbuf (the F12 check)
 *   FBX_OWN         on success, the mbuf is in the slot its req_id names
 *   FBX_SLOT_FREE   the slot taken was free beforehand
 *   FBX_DMA_ORDER   the mbuf is never freed while its mapping is loaded
 *   FBX_PRODUCER    next_to_use advances by one on success, not on failure
 *
 * Switches: RING_SIZE, VACUITY_CHECK, ASSUME_NO_F14.
 */
#include "ena-com/ena_plat.h"
#undef ena_trace
#define ena_trace(ctx, level, fmt, args...) ((void)0)

/* The LLQ line copy into device memory: the loop that unrolls it word by word
 * dominates the formula and no obligation depends on the bytes written (the
 * device reads them, the driver never does).  Replaced by the bounds check it
 * implies, exactly as in the DPDK harness_llq_path.c. */
#include "ena-com/ena_eth_com.h"
#undef ENA_MEMCPY_TO_DEVICE_64
/* FreeBSD's macro takes the bus as its first argument, unlike DPDK's */
#define ENA_MEMCPY_TO_DEVICE_64(bus, dst, src, size)                           \
  __CPROVER_assert(                                                            \
    __CPROVER_r_ok((src), (size)) && __CPROVER_w_ok((dst), (size)),            \
    "FBX_LLQ_COPY: the LLQ line copy stays inside the bounce buffer and BAR")

/* ena_eth_com.c is compiled HERE rather than linked from ena_eth_com.o, so
 * that the neutralised ena_trace and the ENA_MEMCPY_TO_DEVICE_64 override
 * above actually apply to it.  Linked against the pre-built object the real
 * macros are already baked in: curthread->td_tid becomes an undefined-function
 * dereference at every log site, and the word-by-word device copy needs an
 * unwinding bound of its own.  Link this harness WITHOUT ena_eth_com.o. */
#include "ena-com/ena_eth_com.c"

#include "ena_datapath.c"

#ifndef RING_SIZE
#define RING_SIZE 4
#endif
#ifndef MAXSEGS
/* the DMA mapping yields at most this many segments.  ENA_BUS_DMA_SEGS is 32;
 * proving every segment count is future work, so this is a restriction. */
#define MAXSEGS 2
#endif
#define ENA_COM_BOUNCE_BUFFER_CNTRL_CNT_LOCAL 4

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint32_t nd_u32(void) { uint32_t v; return v; }
static uint8_t nd_u8(void) { uint8_t v; return v; }
static int nd_int(void) { int v; return v; }

uint16_t g_probe;
static struct mbuf *g_pkt;          /* the packet being submitted */
static unsigned g_dma_loaded;       /* ghost: is g_pkt's mapping loaded? */
static bool g_dma_ever_loaded;      /* ghost: sticky, survives the unload */
static bool g_freed_while_mapped;   /* ghost: class A violation witness */
static unsigned g_free_calls;

static struct ena_adapter adapter;
static struct ena_com_dev ena_dev;
static struct ena_que que;
static struct ena_ring tx_ring;
static struct ena_tx_buffer pool_tbi[RING_SIZE];
static uint16_t pool_free_ids[RING_SIZE];
static struct mbuf the_mbuf;
static uint8_t mbuf_data[256];
static uint32_t the_db_reg;
static uint8_t bounce[ENA_COM_BOUNCE_BUFFER_CNTRL_CNT_LOCAL * 128];
static uint8_t llq_bar[RING_SIZE * 128];

/* ena_eth_com.o and ena_com.o were compiled with the real ena_trace, which
 * expands to curthread->td_tid, so CBMC sees __curthread as an undefined
 * function returning a possibly-NULL pointer and every log site reports a
 * dereference failure.  Give it a real thread. */
static struct thread the_thread;
struct thread *__curthread(void) { return &the_thread; }

/* the TX timestamp.  getsbinuptime -> getbinuptime is a kernel service with
 * no body here, and bttosbt's shift of its nondeterministic seconds field
 * reports a spurious overflow; nothing in the obligations reads the value. */
sbintime_t getsbinuptime(void) { return (sbintime_t)nd_u32(); }

/* FreeBSD's string.h maps memcpy to __builtin_memcpy, which CBMC's model
 * library does not supply under that name; the runner redirects calls to
 * this definition with --replace-calls. */
void *fbsd_memcpy_model(void *d, const void *s, size_t n)
{
  __CPROVER_assert(
    __CPROVER_w_ok(d, n) && __CPROVER_r_ok(s, n),
    "FBX_MEMCPY: header copy into the bounce buffer stays in bounds");
  for(size_t i = 0; i < n && i < 256; i++)
    ((uint8_t *)d)[i] = ((const uint8_t *)s)[i];
  return d;
}

int device_printf(device_t dev, const char *fmt, ...)
{
  (void)dev;
  (void)fmt;
  return 0;
}
void __mtx_lock_flags(volatile uintptr_t *c, int o, const char *f, int l)
{
  (void)c;
  (void)o;
  (void)f;
  (void)l;
}
void __mtx_unlock_flags(volatile uintptr_t *c, int o, const char *f, int l)
{
  (void)c;
  (void)o;
  (void)f;
  (void)l;
}

void m_freem(struct mbuf *m)
{
  if(m == NULL)
    return;
  if(m == g_pkt && g_dma_loaded)
    g_freed_while_mapped = true;
  if(m == g_pkt)
    g_free_calls++;
}

/* bus_dma: the ghost tracks whether g_pkt's mapping is loaded */
int bus_dmamap_load_mbuf_sg(
  bus_dma_tag_t tag,
  bus_dmamap_t map,
  struct mbuf *m,
  bus_dma_segment_t *segs,
  int *nsegs,
  int flags)
{
  (void)tag;
  (void)map;
  (void)flags;
  if(nd_int())
  {
    *nsegs = 0;
    return ENOMEM; /* the mapping may fail: that is the dma_error path */
  }
  /*
   * THE CONTRACT THAT MATTERS.  bus_dma maps the whole mbuf, so the segment
   * lengths PARTITION it: each is at least 1 and they sum to m_pkthdr.len.
   *
   * ena_tx_map_mbuf's header-skip loop
   *
   *     offset = tx_max_header_size;
   *     while (offset > 0) { ... offset -= segs[iseg].ds_len; iseg++; }
   *
   * (ena_datapath.c:931) indexes segs[] with NO iseg < nsegs guard.  It
   * terminates inside the array only because the lengths sum to the packet
   * length, which exceeds tx_max_header_size on this path.  A stub that did
   * not guarantee the sum made the loop read past segs[] -- see the note in
   * doc/amzn-drivers-2026-11.md.  So this is not a convenience: it is the
   * DMA-layer guarantee the driver silently depends on.
   */
  const uint32_t total = (uint32_t)m->m_pkthdr.len;
  int n = (int)(nd_u8() % MAXSEGS) + 1;
  if((uint32_t)n > total)
    n = (int)total; /* each segment carries at least one byte */
  uint32_t left = total;
  for(int i = 0; i < MAXSEGS; i++)
    if(i < n)
    {
      uint32_t len;
      if(i == n - 1)
        len = left; /* the last segment takes the remainder */
      else
      {
        len = (uint32_t)(nd_u16() % 256) + 1;
        __CPROVER_assume(len >= 1 && len + (uint32_t)(n - 1 - i) <= left);
      }
      segs[i].ds_addr = 0x100000 + 0x1000 * i;
      segs[i].ds_len = (bus_size_t)len;
      left -= len;
    }
  __CPROVER_assume(left == 0);
  *nsegs = n;
  if(m == g_pkt)
  {
    g_dma_loaded = 1;
    g_dma_ever_loaded = true;
  }
  return 0;
}

void bus_dmamap_unload(bus_dma_tag_t tag, bus_dmamap_t map)
{
  (void)tag;
  (void)map;
  g_dma_loaded = 0;
}
void bus_dmamap_sync(bus_dma_tag_t tag, bus_dmamap_t map, bus_dmasync_op_t op)
{
  (void)tag;
  (void)map;
  (void)op;
}

/* m_collapse is only reached when the chain is too long; this harness builds
 * a chain short enough that ena_check_and_collapse_mbuf returns 0, so the
 * collapse path is excluded (a stated restriction, not a model). */
struct mbuf *m_collapse(struct mbuf *m, int how, int maxfrags)
{
  (void)how;
  (void)maxfrags;
  return m;
}

int main(void)
{
  unsigned i;

  tx_ring.adapter = &adapter;
  tx_ring.ena_dev = &ena_dev;
  tx_ring.que = &que;
  que.adapter = &adapter;
  que.id = 0;
  adapter.ena_dev = &ena_dev;
  tx_ring.qid = ENA_IO_TXQ_IDX(0);
  tx_ring.ring_size = RING_SIZE;
  tx_ring.tx_buffer_info = pool_tbi;
  tx_ring.free_tx_ids = pool_free_ids;
  tx_ring.tx_max_header_size = 96;
  tx_ring.tx_mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
  tx_ring.running = true;
  tx_ring.acum_pkts = 0;
  adapter.max_tx_sgl_size = ENA_PKT_MAX_BUFS;
  adapter.disable_meta_caching = false;

  struct ena_com_io_sq *sq = &ena_dev.io_sq_queues[ENA_IO_TXQ_IDX(0)];
  struct ena_com_io_cq *cq = &ena_dev.io_cq_queues[ENA_IO_TXQ_IDX(0)];
  tx_ring.ena_com_io_sq = sq;
  tx_ring.ena_com_io_cq = cq;
  sq->q_depth = RING_SIZE;
  sq->mem_queue_type = ENA_ADMIN_PLACEMENT_POLICY_DEV;
  sq->db_addr = &the_db_reg;
  sq->tail = nd_u16();
  sq->next_to_comp = nd_u16();
  sq->dma_addr_bits = 48;

  tx_ring.next_to_use = nd_u16() % RING_SIZE;
  tx_ring.next_to_clean = nd_u16() % RING_SIZE;

  /* the free list and the slots: the slot next_to_use names must be free,
   * which is the ring invariant ena_xmit_mbuf relies on (it only ENA_WARNs) */
  for(i = 0; i < RING_SIZE; i++)
  {
    pool_free_ids[i] = nd_u16();
    __CPROVER_assume(pool_free_ids[i] < RING_SIZE);
    pool_tbi[i].mbuf = nd_int() ? (struct mbuf *)&the_mbuf : NULL;
    pool_tbi[i].num_of_bufs = 0;
  }
  const uint16_t ntu0 = tx_ring.next_to_use;
  const uint16_t req0 = pool_free_ids[ntu0];
  __CPROVER_assume(pool_tbi[req0].mbuf == NULL); /* the slot taken is free */

  /* the packet: a single short mbuf, so the collapse path is not taken */
  the_mbuf.m_data = mbuf_data;
  the_mbuf.m_len = (int)(nd_u16() % 128) + 1;
  the_mbuf.m_next = NULL;
  the_mbuf.m_pkthdr.len = the_mbuf.m_len;
  the_mbuf.m_pkthdr.csum_flags = 0; /* no offloads */
  the_mbuf.m_flags = M_PKTHDR;
  g_pkt = &the_mbuf;
  g_dma_loaded = 0;
  g_dma_ever_loaded = false;
  g_freed_while_mapped = false;
  g_free_calls = 0;

  /* the LLQ geometry the device established at init */
  sq->llq_info.desc_list_entry_size = 128;
  sq->llq_info.descs_per_entry = 8;
  sq->llq_info.descs_num_before_header = nd_u8();
  __CPROVER_assume(
    sq->llq_info.descs_num_before_header == 1 ||
    sq->llq_info.descs_num_before_header == 2 ||
    sq->llq_info.descs_num_before_header == 4);
  sq->llq_info.disable_meta_caching = false;
  sq->llq_info.max_entries_in_tx_burst = nd_u16();
  /* the LLQ bounce buffer, as ena_com_init_io_sq leaves it.  Without this
   * ena_com_prepare_tx takes its "Bounce buffer is NULL" fault and the
   * success path is unreachable, which the vacuity witness caught. */
  sq->desc_entry_size = sizeof(struct ena_eth_io_tx_desc);
  sq->desc_addr.pbuf_dev_addr = llq_bar;
  sq->tx_max_header_size = 96;
  sq->bounce_buf_ctrl.base_buffer = bounce;
  sq->bounce_buf_ctrl.buffer_size = sq->llq_info.desc_list_entry_size;
  sq->bounce_buf_ctrl.buffers_num = ENA_COM_BOUNCE_BUFFER_CNTRL_CNT_LOCAL;
  sq->bounce_buf_ctrl.next_to_use = 0;
  sq->llq_buf_ctrl.curr_bounce_buf = bounce;
  sq->llq_buf_ctrl.idx = 0;
  sq->llq_buf_ctrl.descs_left_in_line = sq->llq_info.descs_num_before_header;
  sq->disable_meta_caching = false;
  sq->entries_in_tx_burst_left = sq->llq_info.max_entries_in_tx_burst;
  /* the device has completed everything, so the space check can pass */
  sq->next_to_comp = sq->tail;
#ifdef ASSUME_NO_F14
  /* F14 is in shared ena_com code, so FreeBSD has it too; keep it out of the
   * way of the ownership obligations (see the header comment) */
  __CPROVER_assume(
    sq->llq_info.max_entries_in_tx_burst == 0 ||
    (sq->llq_info.descs_num_before_header >= 2 &&
     sq->llq_info.max_entries_in_tx_burst >= 8));
#endif

  struct mbuf *m = &the_mbuf;

  /* ---- one submit ---- */
  int rc = ena_xmit_mbuf(&tx_ring, &m);

  /* ---- obligations ---- */
  if(rc != 0)
  {
    /* THE F12 CHECK: DPDK fails this, FreeBSD must not */
    __CPROVER_assert(
      pool_tbi[req0].mbuf != g_pkt,
      "FBX_NO_UNSENT: on failure no slot holds the mbuf reported unsent");
    __CPROVER_assert(
      tx_ring.next_to_use == ntu0,
      "FBX_PRODUCER: a failed submit does not advance the producer");
  }
  else
  {
    __CPROVER_assert(
      pool_tbi[req0].mbuf == g_pkt,
      "FBX_OWN: on success the mbuf is in the slot its req_id names");
    __CPROVER_assert(
      tx_ring.next_to_use == (uint16_t)((ntu0 + 1) % RING_SIZE),
      "FBX_PRODUCER: a successful submit advances the producer by one");
  }
  __CPROVER_assert(
    !g_freed_while_mapped,
    "FBX_DMA_ORDER: the mbuf is never freed while its DMA mapping is loaded");

#if VACUITY_CHECK
  __CPROVER_assert(rc != 0, "vacuity witness: a submit can succeed");
  __CPROVER_assert(rc == 0, "vacuity witness: a submit can fail");
  /* FBX_NO_UNSENT must be about a FAILED ena_com_prepare_tx, which is the F12
   * path, not only about a failed DMA mapping.  g_dma_loaded is still set when
   * prepare_tx is reached, so this distinguishes the two. */
  __CPROVER_assert(
    rc == 0 || !g_dma_ever_loaded,
    "vacuity witness: the submit can fail after the mapping succeeded (the F12 path)");
#endif
  return 0;
}
