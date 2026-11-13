/*
 * Class S, the largest group the Unveil class list had no name for: an
 * operation the device REJECTED reported to the caller as SUCCESS.  Six of
 * the forty class Z findings are this shape, and two of our own (F18, F21).
 *
 * This one is FreeBSD's SIOCSIFMTU.  `ena_change_mtu` is careful -- it
 * rejects an out-of-range MTU with EINVAL, calls `ena_com_set_dev_mtu`, and
 * only calls `if_setmtu` when the device accepted (ena.c:331-352).  Its
 * caller then throws that away (ena.c:2420-2430):
 *
 *     case SIOCSIFMTU:
 *             if (if_getmtu(ifp) == ifr->ifr_mtu)
 *                     break;
 *             ENA_LOCK_LOCK();
 *             ena_down(adapter);
 *             ena_change_mtu(ifp, ifr->ifr_mtu);     <-- return value dropped
 *             rc = ena_up(adapter);
 *             ENA_LOCK_UNLOCK();
 *             break;
 *
 * so the ioctl's result is `ena_up`'s, and a rejected MTU is reported as
 * success while the interface keeps the MTU it had.
 *
 *   SM_RC_SOUND   the ioctl returns 0 only if the interface MTU is now the
 *                 one that was asked for
 *   SM_NO_SILENT  the ioctl does not return 0 when the device rejected the
 *                 MTU
 *   SM_MTU_RANGE  the MTU handed to the device is within what the device
 *                 advertised -- the check ena_change_mtu does make
 *
 * -DWITH_RC_FIX propagates ena_change_mtu's result, which is the fix, so the
 *  obligations distinguish the bug from the repair rather than merely failing.
 *
 * Switches: VACUITY_CHECK, WITH_RC_FIX.
 */
#include "ena-com/ena_plat.h"
#undef ena_trace
#define ena_trace(ctx, level, fmt, args...) ((void)0)

/* ena.c is compiled in rather than linked from ena.o because ena_ioctl is
 * static; linking the object would leave it unreachable. */
#include "ena.c"

static int nd_int(void) { int v; return v; }
static uint16_t nd_u16(void) { uint16_t v; return v; }

#define MAX_MTU 9000

static struct ena_adapter sm_adapter;
static struct ifnet sm_ifp_storage;
static if_t sm_ifp = &sm_ifp_storage;

/* ---- ghost state: what the device was asked, and what it answered ---- */
static int g_device_asked_mtu;
static bool g_device_was_asked;
static bool g_device_accepted;
static uint16_t g_iface_mtu;

/* The device's answer is the point of the whole obligation, so it is
 * nondeterministic: a device may reject an MTU the driver considers legal. */
int ena_com_set_dev_mtu(struct ena_com_dev *d, uint32_t mtu)
{
  (void)d;
  g_device_asked_mtu = (int)mtu;
  g_device_was_asked = true;
  __CPROVER_assert(
    mtu <= MAX_MTU && mtu >= ENA_MIN_MTU,
    "SM_MTU_RANGE: the MTU handed to the device is one it advertised");
  if(nd_int())
  {
    g_device_accepted = false;
    return ENA_COM_INVAL;
  }
  g_device_accepted = true;
  return 0;
}

/* the interface MTU, which is what a caller observes */
int if_getmtu(const if_t ifp)
{
  (void)ifp;
  return (int)g_iface_mtu;
}
int if_setmtu(if_t ifp, int mtu)
{
  (void)ifp;
  g_iface_mtu = (uint16_t)mtu;
  return 0;
}
/* reached on the rejection path; a bodyless callee there would cut exactly
 * the path the obligations are about */
int device_printf(device_t dev, const char *fmt, ...)
{
  (void)dev;
  (void)fmt;
  return 0;
}
void *if_getsoftc(if_t ifp)
{
  (void)ifp;
  return &sm_adapter;
}

/* bring-down and bring-up are not under test here; what matters is that
 * ena_up can succeed, because that is the result the ioctl returns in place
 * of the one it dropped */
static void sm_down(struct ena_adapter *a) { (void)a; }
static int sm_up(struct ena_adapter *a)
{
  (void)a;
  return 0;
}

int main(void)
{
  sm_adapter.pdev = NULL;
  sm_adapter.ifp = sm_ifp;
  sm_adapter.max_mtu = MAX_MTU;

  g_iface_mtu = nd_u16();
  __CPROVER_assume(g_iface_mtu >= ENA_MIN_MTU && g_iface_mtu <= MAX_MTU);
  const uint16_t mtu_before = g_iface_mtu;

  struct ifreq req;
  req.ifr_mtu = (int)nd_u16();
  /* a caller may ask for anything; the driver is responsible for refusing */
  g_device_was_asked = false;
  g_device_accepted = false;
  g_device_asked_mtu = 0;

  /* ---- the ioctl's MTU case, as the driver runs it ---- */
  int rc;
  if(if_getmtu(sm_ifp) == req.ifr_mtu)
  {
    rc = 0; /* nothing to do, and nothing to get wrong */
  }
  else
  {
    sm_down(&sm_adapter);
    const int change_rc = ena_change_mtu(sm_ifp, req.ifr_mtu);
#ifdef WITH_RC_FIX
    /* The fix: do not discard it.  The driver currently ignores change_rc
     * and returns ena_up's result instead. */
    if(change_rc != 0)
      rc = change_rc;
    else
      rc = sm_up(&sm_adapter);
#else
    (void)change_rc;
    rc = sm_up(&sm_adapter);
#endif
  }

  /* ---- the class S obligations ---- */
  __CPROVER_assert(
    rc != 0 || g_iface_mtu == (uint16_t)req.ifr_mtu,
    "SM_RC_SOUND: the ioctl returns 0 only if the interface MTU is the one "
    "that was asked for");
  __CPROVER_assert(
    rc != 0 || !(g_device_was_asked && !g_device_accepted),
    "SM_NO_SILENT: the ioctl does not return 0 when the device rejected the "
    "MTU");

#ifdef VACUITY_CHECK
  /* All must FAIL.  Without them the obligations above could hold because no
   * MTU change is ever attempted, or because the device never refuses. */
  __CPROVER_assert(
    !g_device_was_asked, "witness: the device can be asked to set an MTU");
  __CPROVER_assert(
    g_device_accepted || !g_device_was_asked,
    "witness: the device can refuse an MTU");
  __CPROVER_assert(
    g_iface_mtu == mtu_before, "witness: the interface MTU can change");
#endif
  return 0;
}
