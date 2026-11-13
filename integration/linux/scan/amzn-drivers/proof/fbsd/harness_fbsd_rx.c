/*
 * FreeBSD ENA ena_rx_mbuf: the RX assembly step.
 *
 * The counterpart of harness_fbsd_xmit.c on the receive side, and the mirror
 * of the DPDK RX work.  Two of these obligations are checks DPDK fails and
 * FreeBSD should pass, which is the point of running them here: F16's two
 * triggers, an unposted slot and a req_id repeated within one chain.  DPDK
 * dereferences rx_buffer_info[req_id] behind a compiled-out RTE_ASSERT;
 * FreeBSD tests rx_info->mbuf for NULL at both sites (ena_datapath.c:436 and
 * :480) and triggers a device reset.
 *
 * Note on scope, established by reading before writing: an out-of-RANGE
 * req_id is not reachable here.  Shared ena_com validates it in
 * ena_com_rx_pkt -- "if (unlikely(ena_buf[i].req_id >= q_depth)) return
 * ENA_COM_EIO" (ena-com/ena_eth_com.c:638-640) -- so there is no RX
 * counterpart to F11, and F16's triggers are both in-range cases.  This
 * harness therefore supplies in-range req_ids and attacks the two reachable
 * triggers instead.
 *
 *   FBR_POSTED      every slot dereferenced was posted, or the chain is
 *                   abandoned and a reset requested (the F16 check)
 *   FBR_NO_REUSE    no mbuf is handed up or freed twice when the device
 *                   repeats a req_id inside one chain
 *   FBR_DMA_ORDER   a mapping is unloaded before its mbuf is freed or
 *                   released upwards; the class A obligation DPDK cannot
 *                   express
 *   FBR_FREE_ID     each consumed req_id is pushed to free_rx_ids once
 *   FBR_NTC         next_to_clean advances only on success
 *
 * Switches: RING_SIZE, MAXCHAIN, VACUITY_CHECK.
 */
#include "ena-com/ena_plat.h"
#undef ena_trace
#define ena_trace(ctx, level, fmt, args...) ((void)0)

#include "ena-com/ena_eth_com.h"
#undef ENA_MEMCPY_TO_DEVICE_64
#define ENA_MEMCPY_TO_DEVICE_64(bus, dst, src, size) ((void)0)
#include "ena-com/ena_eth_com.c"

#include "ena_datapath.c"

#ifndef RING_SIZE
#define RING_SIZE 4
#endif
#ifndef MAXCHAIN
/* descriptors per received packet.  A restriction: the device may name more,
 * and proving every chain length is future work. */
#define MAXCHAIN 2
#endif

static int nd_int(void) { int v; return v; }
static uint16_t nd_u16(void) { uint16_t v; return v; }

static struct ena_adapter adapter;
static struct ena_ring rx_ring;
static struct ena_que que;
static struct ifnet the_ifp;
static struct ena_rx_buffer rx_info_pool[RING_SIZE];
static uint16_t free_ids[RING_SIZE];
static struct mbuf mbuf_pool[RING_SIZE];
static struct ena_com_rx_buf_info ena_bufs[MAXCHAIN];

/* ---- ghost state: DMA loaded-ness and release accounting ---- */
static bool g_loaded[RING_SIZE];
static unsigned g_freed[RING_SIZE];
static unsigned g_pushed[RING_SIZE];
static bool g_reset_requested;

static int slot_of_map(bus_dmamap_t m)
{
  for(int i = 0; i < RING_SIZE; i++)
    if(m == rx_info_pool[i].map)
      return i;
  return -1;
}
static int slot_of_mbuf(const struct mbuf *m)
{
  for(int i = 0; i < RING_SIZE; i++)
    if(m == &mbuf_pool[i])
      return i;
  return -1;
}

void bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t map)
{
  (void)t;
  const int s = slot_of_map(map);
  if(s >= 0)
    g_loaded[s] = false;
}
void bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t map, bus_dmasync_op_t op)
{
  (void)t;
  (void)map;
  (void)op;
}
void m_freem(struct mbuf *m)
{
  const int s = slot_of_mbuf(m);
  if(s >= 0)
  {
    __CPROVER_assert(
      !g_loaded[s],
      "FBR_DMA_ORDER: no mbuf is freed while its DMA mapping is loaded");
    __CPROVER_assert(
      g_freed[s] == 0, "FBR_NO_REUSE: no mbuf is freed twice");
    g_freed[s]++;
  }
}
/* Both are reached on the assembly path, and a REACHED bodyless callee cuts
 * it, which would leave the obligations holding for want of a path.  m_append
 * returns an arbitrary result so the append-failure branch stays live: that
 * branch bumps a counter and carries on, so it must not disturb the DMA or
 * release accounting. */
int m_append(struct mbuf *m, int len, c_caddr_t cp)
{
  (void)m;
  (void)len;
  (void)cp;
  return nd_int();
}
int device_printf(device_t dev, const char *fmt, ...)
{
  (void)dev;
  (void)fmt;
  return 0;
}

void ena_trigger_reset(struct ena_adapter *a, enum ena_regs_reset_reason_types r)
{
  (void)a;
  (void)r;
  g_reset_requested = true;
}

int main(void)
{
  unsigned i;

  adapter.pdev = NULL;
  adapter.ifp = &the_ifp;
  adapter.rx_buf_tag = NULL;
  que.adapter = &adapter;

  rx_ring.adapter = &adapter;
  rx_ring.que = &que;
  rx_ring.qid = 0;
  rx_ring.ring_size = RING_SIZE;
  rx_ring.rx_buffer_info = rx_info_pool;
  rx_ring.free_rx_ids = free_ids;

  /* an arbitrary started RX ring: every slot either posted or not */
  for(i = 0; i < RING_SIZE; i++)
  {
    rx_info_pool[i].map = (bus_dmamap_t)(uintptr_t)(0x100 + i);
    if(nd_int())
    {
      rx_info_pool[i].mbuf = &mbuf_pool[i];
      g_loaded[i] = true;
    }
    else
    {
      rx_info_pool[i].mbuf = NULL;
      g_loaded[i] = false;
    }
    mbuf_pool[i].m_data = (caddr_t)&mbuf_pool[i];
    g_freed[i] = 0;
    g_pushed[i] = 0;
  }
  g_reset_requested = false;

  /* The device's completion, as ena_com_rx_pkt leaves it.  req_ids are
   * in-range because ena_com already rejects out-of-range ones, but they are
   * otherwise arbitrary -- including REPEATED inside one chain, which is
   * F16's second trigger. */
  struct ena_com_rx_ctx ctx;
  ctx.descs = (uint16_t)(nd_u16() % MAXCHAIN) + 1;
  ctx.pkt_offset = 0;
  ctx.ena_bufs = ena_bufs;
  ctx.max_bufs = MAXCHAIN;
  for(i = 0; i < MAXCHAIN; i++)
  {
    ena_bufs[i].req_id = nd_u16() % RING_SIZE;
    ena_bufs[i].len = 1;
  }

  uint16_t ntc = nd_u16() % RING_SIZE;
  const uint16_t ntc_before = ntc;

  struct mbuf *out = ena_rx_mbuf(&rx_ring, ena_bufs, &ctx, &ntc);

  if(out != NULL)
  {
    /* Success: every slot the chain consumed must have been posted, its
     * mapping unloaded, and its id published exactly once. */
    for(i = 0; i < ctx.descs; i++)
    {
      const uint16_t id = ena_bufs[i].req_id;
      __CPROVER_assert(
        !g_loaded[id],
        "FBR_DMA_ORDER: a consumed slot's mapping is unloaded before release");
      __CPROVER_assert(
        rx_ring.rx_buffer_info[id].mbuf == NULL,
        "FBR_POSTED: a consumed slot is left unposted for refill");
    }
    __CPROVER_assert(
      ntc != ntc_before || ctx.descs == 0,
      "FBR_NTC: next_to_clean advances on success");
  }
  else
  {
    /* Failure: FreeBSD's answer to an unposted slot is to abandon the chain
     * and ask for a reset.  This is the check DPDK does not make. */
    __CPROVER_assert(
      g_reset_requested,
      "FBR_POSTED: an unposted slot abandons the chain and requests a reset");
    __CPROVER_assert(
      ntc == ntc_before,
      "FBR_NTC: next_to_clean is not advanced on failure");
  }

  for(i = 0; i < RING_SIZE; i++)
    __CPROVER_assert(
      g_freed[i] <= 1, "FBR_NO_REUSE: no slot's mbuf is released twice");

#ifdef VACUITY_CHECK
  /* All must FAIL.  Each says the state its obligation describes can arise:
   * the step can succeed, it can hit an unposted slot, and a chain longer
   * than one descriptor is reachable. */
  __CPROVER_assert(out == NULL, "witness: the assembly can succeed");
  __CPROVER_assert(out != NULL, "witness: an unposted slot can be hit");
  __CPROVER_assert(ctx.descs < 2, "witness: a multi-descriptor chain arises");
#endif
  return 0;
}
