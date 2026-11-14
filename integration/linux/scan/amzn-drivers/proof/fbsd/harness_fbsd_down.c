/*
 * FreeBSD's ena_down against its TX datapath -- the counterpart of
 * harness_conc_datapath.c, and the reason F28 is worth reporting rather than
 * shrugging at.
 *
 * F28 is two defects in DPDK's ena_stop / eth_ena_xmit_pkts pair:
 *
 *   1. the datapath's state check is compiled out, being under
 *      RTE_ETHDEV_DEBUG_TX;
 *   2. even compiled in it would not help, because ena_stop assigns
 *      ENA_ADAPTER_STATE_STOPPED nineteen lines AFTER destroying the queues.
 *
 * FreeBSD gets both halves right, in the same driver family:
 *
 *   * ena_mq_start tests IFF_DRV_RUNNING unconditionally and returns ENODEV
 *     when it is clear (ena_datapath.c:149-150) -- no debug guard;
 *   * ena_down clears IFF_DRV_RUNNING via if_setdrvflagbits BEFORE it frees
 *     the IRQs and destroys the io queues (ena.c:2634-2645).
 *
 * So this harness should PROVE the obligation DPDK fails, which makes the fix
 * concrete: do what the sibling driver does, on both counts.
 *
 *   FBD_NO_SUBMIT_AFTER_DESTROY  no transmit is submitted to an io queue that
 *                                ena_down has destroyed
 *   FBD_FLAG_BEFORE_TEARDOWN     IFF_DRV_RUNNING is clear by the time the
 *                                queues are destroyed
 *
 * -DBREAK_ORDER moves the flag clear after the teardown, i.e. gives FreeBSD
 *  DPDK's ordering, so the obligation distinguishes the two designs rather
 *  than merely passing.
 *
 * Note on the flag: the datapath reads it through if_getdrvflags, which is an
 * ordinary function, so this harness is unaffected by the inline-assembly
 * problem that makes ENA_FLAG_*_ATOMIC invisible to CBMC
 * (cbmc-limitations/inline_asm_assumed_no_effect.md).  The driver's own
 * ENA_FLAG_CLEAR_ATOMIC(ENA_FLAG_DEV_UP) is not what guards the datapath.
 *
 * Switches: WHEN (0,1,2 -- where the transmit lands), VACUITY_CHECK,
 * BREAK_ORDER.
 */
#include "ena-com/ena_plat.h"
#undef ena_trace
#define ena_trace(ctx, level, fmt, args...) ((void)0)

#ifndef WHEN
#define WHEN 1
#endif

static int nd_int(void) { int v; return v; }

/* ---- the interface flags the datapath actually tests ---- */
static int g_drvflags;
static bool g_io_destroyed;
static unsigned g_submits_after_destroy;
static bool g_attempted;
static bool g_accepted;

int if_getdrvflags(const if_t ifp)
{
  (void)ifp;
  return g_drvflags;
}
int if_setdrvflagbits(if_t ifp, int set, int clear)
{
  (void)ifp;
  g_drvflags = (g_drvflags | set) & ~clear;
  return 0;
}

#include "ena-com/ena_eth_com.h"
#undef ENA_MEMCPY_TO_DEVICE_64
#define ENA_MEMCPY_TO_DEVICE_64(bus, dst, src, size) ((void)0)

/* Only the interface handle is needed: the obligation is about the flag the
 * datapath tests and the order ena_down touches it in.  struct ena_adapter is
 * not pulled in, so the harness stays independent of ena.h. */
static uint8_t fbd_ifp_storage[256];
static if_t fbd_ifp = (if_t)fbd_ifp_storage;

int _sx_xlock(struct sx *sx, int opts, const char *file, int line)
{
  (void)sx;
  (void)opts;
  (void)file;
  (void)line;
  return 0;
}
void _sx_xunlock(struct sx *sx, const char *file, int line)
{
  (void)sx;
  (void)file;
  (void)line;
}
int device_printf(device_t dev, const char *fmt, ...)
{
  (void)dev;
  (void)fmt;
  return 0;
}

/* The submit path: a submission after the queues are destroyed is the thing
 * the obligation is about, so make it visible. */
int ena_com_prepare_tx(
  struct ena_com_io_sq *io_sq, struct ena_com_tx_ctx *ctx, uint16_t *nb_hw_desc)
{
  (void)io_sq;
  (void)ctx;
  if(g_io_destroyed)
    g_submits_after_destroy++;
  __CPROVER_assert(
    !g_io_destroyed,
    "FBD_NO_SUBMIT_AFTER_DESTROY: no transmit is submitted to an io queue "
    "that ena_down has destroyed");
  *nb_hw_desc = 1;
  return 0;
}

/* ena_down's two relevant steps, in the driver's order (ena.c:2634-2645):
 * the flag is cleared first, the queues destroyed last. */
static void down_clear_flag(void)
{
  if_setdrvflagbits(fbd_ifp, IFF_DRV_OACTIVE, IFF_DRV_RUNNING);
}
static bool g_flag_set_at_destroy;
static void down_destroy_queues(void)
{
  /* Capture the flag AT the moment of teardown.  Checking it at the end of
   * main instead would pass in both orderings, since both clear it
   * eventually -- the question is whether it is clear BY THEN. */
  g_flag_set_at_destroy = (g_drvflags & IFF_DRV_RUNNING) != 0;
  g_io_destroyed = true;
}

/* ena_mq_start's guard, verbatim from ena_datapath.c:149-150, which is
 * unconditional in the real driver -- no debug #ifdef. */
static void datapath_step(void)
{
  g_attempted = true;
  if((if_getdrvflags(fbd_ifp) & IFF_DRV_RUNNING) == 0)
    return; /* ENODEV */
  g_accepted = true;
  {
    struct ena_com_io_sq sq;
    struct ena_com_tx_ctx ctx;
    uint16_t n;
    ena_com_prepare_tx(&sq, &ctx, &n);
  }
}

int main(void)
{
  g_drvflags = IFF_DRV_RUNNING;
  g_io_destroyed = false;
  g_submits_after_destroy = 0;
  g_attempted = false;
  g_accepted = false;

#ifdef BREAK_ORDER
  /* DPDK's ordering, for contrast: tear down first, clear the flag after. */
#  if WHEN == 0
  datapath_step();
  down_destroy_queues();
  down_clear_flag();
#  elif WHEN == 1
  down_destroy_queues();
  datapath_step();
  down_clear_flag();
#  else
  down_destroy_queues();
  down_clear_flag();
  datapath_step();
#  endif
#else
  /* FreeBSD's ordering: clear the flag, then tear down. */
#  if WHEN == 0
  datapath_step();
  down_clear_flag();
  down_destroy_queues();
#  elif WHEN == 1
  down_clear_flag();
  datapath_step();
  down_destroy_queues();
#  else
  down_clear_flag();
  down_destroy_queues();
  datapath_step();
#  endif
#endif

  __CPROVER_assert(
    !g_flag_set_at_destroy,
    "FBD_FLAG_BEFORE_TEARDOWN: IFF_DRV_RUNNING is clear by the time the "
    "queues are destroyed");

#ifdef VACUITY_CHECK
  /* Must FAIL: the datapath really runs. */
  __CPROVER_assert(!g_attempted, "witness: the datapath is attempted");
  /* The transmit is accepted exactly when it runs BEFORE the flag is cleared.
   * FreeBSD clears first, so only WHEN 0 precedes it; DPDK's order clears
   * last, so WHEN 0 and 1 do.  Keying the witness on WHEN alone, or on the
   * ordering alone, mislabelled one run each time -- the condition is the
   * conjunction. */
#  if WHEN == 0 || (defined(BREAK_ORDER) && WHEN == 1)
  __CPROVER_assert(!g_accepted, "witness: the transmit can be accepted");
#  else
  __CPROVER_assert(g_accepted, "witness: the transmit is declined");
#  endif
#endif
  return 0;
}
