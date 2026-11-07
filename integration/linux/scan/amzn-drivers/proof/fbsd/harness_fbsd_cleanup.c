/*
 * FreeBSD ENA: ena_tx_cleanup, with the free discipline and the DMA-ownership
 * obligation.
 *
 * Why FreeBSD and why this function.  ena_com is shared between the DPDK,
 * Linux and FreeBSD drivers and differs by about 8%, so the ena_com
 * invariants proven for DPDK largely carry over.  What does not carry over is
 * the OS glue, and this is the first glue function examined.
 *
 * Two things differ from the DPDK cleanup in ways the proof can see:
 *
 *   1. FreeBSD clears tx_info->mbuf BEFORE m_freem(), where DPDK frees first
 *      and clears after.  FreeBSD's order is the safer one and there is no
 *      window in which a slot points at freed memory.
 *   2. FreeBSD unmaps the DMA mapping (bus_dmamap_unload) before freeing the
 *      mbuf.  That ordering is the entire defence against Unveil class A,
 *      device DMA into freed memory, and it is stated here as
 *      DMA_UNMAPPED_BEFORE_FREE -- an obligation the DPDK harness could not
 *      express because DPDK has no bus_dma layer.
 *
 * DEVICE TRUST.  The completion descriptor ring is written by the device, so
 * the harness makes the descriptor memory nondeterministic rather than
 * modelling ena_com_tx_comp_req_id_get.  The real ena_com code then runs on
 * device-chosen bytes, which is what exposes F11.
 */
/* ena_tx_cleanup is static, so the translation unit is included wholesale,
 * the same way the DPDK harnesses include ena_ethdev.c.  Link this harness
 * WITHOUT ena_datapath.o to avoid duplicate symbols. */
/*
 * Neutralise the ena_com trace macros before any header that USES them is
 * preprocessed.  ena_trace expands to ena_log((ctx)->dmadev, ...), and ctx is
 * the pointer ENA's container_of() builds by casting through uintptr_t:
 *
 *   container_of(io_cq, struct ena_com_dev, io_cq_queues[io_cq->qid])
 *
 * CBMC loses object identity across that integer cast, so every field read
 * through ctx becomes an 8-byte byte_extract at a symbolic offset over the
 * whole 99,648-byte struct ena_com_dev.  Two such reads in
 * ena_com_tx_comp_req_id_get accounted for 79% of the 135M clauses in this
 * proof.  Logging has no part in any obligation here, so dropping the
 * expansion costs nothing and is not an abstraction of driver behaviour.
 *
 * ena_plat.h is included first so that its include guard stops the real
 * definitions being restored underneath us.
 */
#include "ena-com/ena_plat.h"
#undef ena_trace
#define ena_trace(ctx, level, fmt, args...) ((void)0)

#include "ena_datapath.c"

static uint16_t nd_u16(void) { uint16_t v; return v; }
static uint8_t nd_u8(void) { uint8_t v; return v; }
static int nd_int(void) { int v; return v; }

#ifndef RING_SIZE
#define RING_SIZE 8
#endif

/* ---------------- ghost state for the ownership discipline -------------- */
static struct mbuf *g_watch;      /* an arbitrary mbuf to follow */
uint16_t g_probe;                 /* an arbitrary slot index */
static unsigned g_free_calls;     /* times g_watch was freed */
static unsigned g_dma_loaded;     /* ghost: is g_watch's mapping loaded? */
static bool g_freed_while_mapped; /* ghost: class A violation witness */

static struct ena_adapter adapter;
static struct ena_com_dev ena_dev;
static struct ena_que que;
static struct ena_ring tx_ring;
static struct ena_tx_buffer pool_tbi[RING_SIZE];
static uint16_t pool_free_ids[RING_SIZE];
static struct mbuf pool_mbuf[RING_SIZE];

/* The completion queue descriptor ring.  Device-writable memory. */
static struct ena_eth_io_tx_cdesc pool_cdesc[RING_SIZE];

/*
 * Kernel functions with no body in this build.  Left undefined they are
 * turned into assert(false); assume(false), which CUTS the paths they sit on
 * and silently degrades every obligation downstream (DFCC issues A and C).
 * device_printf sits on the invalid-req_id path, and the mutex pair wraps the
 * ring-state update at the end of cleanup, so all three are directly on the
 * paths this proof is about.
 */
int device_printf(device_t dev, const char *fmt, ...)
{
  (void)dev;
  (void)fmt;
  return 0;
}

void __mtx_lock_flags(
  volatile uintptr_t *c,
  int opts,
  const char *file,
  int line)
{
  (void)c;
  (void)opts;
  (void)file;
  (void)line;
}

void __mtx_unlock_flags(
  volatile uintptr_t *c,
  int opts,
  const char *file,
  int line)
{
  (void)c;
  (void)opts;
  (void)file;
  (void)line;
}

/*
 * m_freem is the only place this path releases an mbuf, so the free discipline
 * is checked here, where a double free is observable.
 */
void m_freem(struct mbuf *m)
{
  if(m == NULL)
    return;

  __CPROVER_assert(
    m != g_watch || g_free_calls == 0,
    "FREE_ONCE: no mbuf is released twice");

  /*
   * Class A: releasing an mbuf whose DMA mapping is still loaded leaves the
   * device able to write into memory the kernel has handed back.
   */
  if(m == g_watch && g_dma_loaded)
    g_freed_while_mapped = true;

  if(m == g_watch)
    g_free_calls++;
}

/* bus_dma: track the mapping state of the watched mbuf. */
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

int main(void)
{
  unsigned i;
  bool rc;

  /* Havoc the device-side state ONCE, before any field is written.
   * __CPROVER_havoc_object havocs the WHOLE enclosing object, so doing this
   * after the assignments below wipes them -- which silently made this
   * harness vacuous until the vacuity witness caught it. */
  __CPROVER_havoc_object(&ena_dev);

  /* ---- a started TX queue in a consistent state ---- */
  tx_ring.adapter = &adapter;
  tx_ring.que = &que;
  que.adapter = &adapter;
  que.id = 0;
  tx_ring.qid = 0;
  tx_ring.ring_size = RING_SIZE;
  tx_ring.tx_buffer_info = pool_tbi;
  tx_ring.free_tx_ids = pool_free_ids;
  adapter.ena_dev = &ena_dev;
  /* the tail of ena_tx_cleanup reads tx_ring->ena_com_io_sq; tx_ring is a
   * zero-initialised static, so leaving these unset made that a NULL read */
  tx_ring.ena_com_io_sq = &ena_dev.io_sq_queues[0];
  tx_ring.ena_com_io_cq = &ena_dev.io_cq_queues[0];
  tx_ring.running = true;

  /* the completion queue the device writes into */
  ena_dev.io_cq_queues[0].q_depth = RING_SIZE;
  ena_dev.io_cq_queues[0].cdesc_addr.virt_addr = (uint8_t *)pool_cdesc;
  ena_dev.io_cq_queues[0].cdesc_entry_size_in_bytes = sizeof(pool_cdesc[0]);
  /* qid must be CONCRETE: ena_com derives the containing ena_com_dev with
   * container_of(io_cq, struct ena_com_dev, io_cq_queues[io_cq->qid]), so a
   * symbolic qid makes every field read through that pointer an 8-byte
   * byte_extract at a symbolic offset over a 99,648-byte struct.  Three such
   * reads accounted for 97% of the clauses in the first attempt. */
  ena_dev.io_cq_queues[0].qid = 0;
  ena_dev.io_cq_queues[0].head = 0;
  ena_dev.io_cq_queues[0].phase = 1;
  ena_dev.io_sq_queues[0].q_depth = RING_SIZE;

  tx_ring.next_to_clean = nd_u16() & (RING_SIZE - 1);

  /*
   * INV_DEV_LLQ, carried over from the DPDK proof (ena_inv.h): a valid
   * LLQ-mode SQ has descs_per_entry >= 1, because ena_com_sq_have_enough_space
   * divides by it.  Established by ena_com_config_dev_mode at device init,
   * which this harness does not perform, so it is assumed here.
   */
  __CPROVER_assume(ena_dev.io_sq_queues[0].llq_info.descs_per_entry >= 1);
  __CPROVER_assume(ena_dev.io_sq_queues[0].llq_info.descs_per_entry <= 16);

  /* ---- arbitrary slot occupancy, one distinct mbuf per slot ---- */
  for(i = 0; i < RING_SIZE; i++)
  {
    pool_tbi[i].mbuf = nd_int() ? &pool_mbuf[i] : NULL;
    pool_tbi[i].tx_descs = nd_u16();
    __CPROVER_assume(pool_tbi[i].tx_descs <= 17);
    pool_free_ids[i] = nd_u16();
    __CPROVER_assume(pool_free_ids[i] < RING_SIZE);
  }

  /*
   * The device fills the completion descriptors.  Every byte is arbitrary,
   * which is the point: the proof must hold for a device that writes
   * anything at all, including a req_id outside the ring.
   */
  __CPROVER_havoc_object(pool_cdesc);

  g_probe = nd_u16() % RING_SIZE;
  g_watch = &pool_mbuf[nd_u16() % RING_SIZE];
  g_free_calls = 0;
  g_dma_loaded = 1; /* an in-flight packet has its mapping loaded */
  g_freed_while_mapped = false;

  rc = ena_tx_cleanup(&tx_ring);
  (void)rc;

  /* ---- obligations ---- */
#if VACUITY_CHECK
  /* must FAIL: a SUCCESS here means main never reaches the obligations and
   * every one of them is about an unreachable state.  A contradictory
   * __CPROVER_assume is the usual cause -- it happened here once, when
   * descs_per_entry was assumed >= 1 on a zero-initialised static. */
  __CPROVER_assert(0, "vacuity witness: the obligations are reachable");
#endif

  __CPROVER_assert(
    !g_freed_while_mapped,
    "DMA_UNMAPPED_BEFORE_FREE: no mbuf is released while the device can "
    "still write to it");

  __CPROVER_assert(
    tx_ring.next_to_clean < RING_SIZE,
    "INV_NTC: the consumer index stays inside the ring");

  __CPROVER_assert(
    g_free_calls == 0 || g_probe >= RING_SIZE ||
      pool_tbi[g_probe].mbuf != g_watch,
    "OWN_NO_DANGLING: no slot still owns a released mbuf");

  return 0;
}
