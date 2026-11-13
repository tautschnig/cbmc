/*
 * Class S: FreeBSD's stats_sample_interval sysctl reports success for a write
 * it rejected.
 *
 * Confirms the Velocity Labs class Z finding "Rejected stats_sample_interval
 * writes report success to sysctl(8) and scripts".  Theirs, not ours; what is
 * added here is the proof and the located cause.
 *
 * ena_sysctl_metrics_interval computes `error` carefully on three paths --
 * the device is not running, sysctl_handle_16 fails, or the interval exceeds
 * ENA_METRICS_MAX_SAMPLE_INTERVAL -- and then discards it
 * (ena_sysctl.c:886-931):
 *
 *     if (unlikely(!ENA_FLAG_ISSET(ENA_FLAG_DEVICE_RUNNING, adapter))) {
 *             error = EINVAL;
 *             goto unlock;
 *     }
 *     ...
 *     if (interval > ENA_METRICS_MAX_SAMPLE_INTERVAL) {
 *             error = EINVAL;
 *             goto unlock;
 *     }
 *     ...
 *     adapter->metrics_sample_interval = interval;
 *
 *     unlock:
 *             ENA_LOCK_UNLOCK();
 *             return (0);        <-- error never reaches the caller
 *
 * So sysctl(8) and any script driving it are told the write succeeded while
 * the interval keeps its old value.  This is the same shape as F26 in DPDK's
 * ena_stop (a status computed, logged or not, and dropped) and as the MTU
 * ioctl discarding ena_change_mtu's result.
 *
 *   SS_RC_SOUND   the handler returns 0 only if the interval it was asked for
 *                 was actually applied
 *   SS_NO_SILENT  the handler does not return 0 on a write it rejected
 *   SS_RANGE      an applied interval is within what the driver accepts --
 *                 the check the handler does make
 *
 * -DWITH_RC_FIX returns `error` instead of 0, which is the fix.
 *
 * Switches: VACUITY_CHECK, WITH_RC_FIX.
 */
#include "ena-com/ena_plat.h"
#undef ena_trace
#define ena_trace(ctx, level, fmt, args...) ((void)0)

static int nd_int(void) { int v; return v; }
static uint16_t nd_u16(void) { uint16_t v; return v; }

/* ---- ghost state: what the write asked for, and whether it was taken ---- */
static uint16_t g_requested;
static bool g_handler_rejected;

/* sysctl_handle_16 is where the new value arrives from userspace.  It may
 * also fail, which is one of the three paths whose status is dropped. */
int sysctl_handle_16(struct sysctl_oid *oidp, void *arg1, intmax_t arg2,
                     struct sysctl_req *req)
{
  (void)oidp;
  (void)arg2;
  (void)req;
  if(nd_int())
  {
    g_handler_rejected = true;
    return EINVAL;
  }
  if(arg1 != NULL)
    *(uint16_t *)arg1 = g_requested;
  return 0;
}
int sysctl_wire_old_buffer(struct sysctl_req *req, size_t len)
{
  (void)req;
  (void)len;
  return 0;
}
int device_printf(device_t dev, const char *fmt, ...)
{
  (void)dev;
  (void)fmt;
  return 0;
}

/* ENA_LOCK_LOCK expands to sx_xlock, whose out-of-line helpers have no body
 * in this build.  A REACHED bodyless callee cuts the path, which would leave
 * the obligations below holding for want of a path rather than because the
 * handler is correct -- and the first run of this harness showed exactly that
 * asymmetry, one obligation failing and the other passing. */
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
void _sx_sunlock(struct sx *sx, const char *file, int line)
{
  (void)sx;
  (void)file;
  (void)line;
}

#include "ena_sysctl.c"

static struct ena_adapter ss_adapter;
static struct sysctl_req ss_req;
static struct sysctl_oid ss_oid;

int main(void)
{
  ss_adapter.pdev = NULL;
  /* The device may or may not be running: one of the rejection paths.
   *
   * BIT_SET, not ENA_FLAG_SET_ATOMIC.  The atomic form goes through
   * FreeBSD's atomic_set_long, which amd64 generates from ATOMIC_ASM --
   * inline assembly, which CBMC cannot interpret and so silently treats as a
   * no-op.  The flag was never observed set, the running path was never
   * taken, and sysctl_handle_16 was never called, which made one obligation
   * vacuous.  Nothing reported this: the run was clean and one obligation
   * even failed, so the verdict looked informative. */
  if(nd_int())
    BIT_SET(ENA_FLAGS_NUMBER, ENA_FLAG_DEVICE_RUNNING, &ss_adapter.flags);

  const uint16_t before = nd_u16();
  __CPROVER_assume(before <= ENA_METRICS_MAX_SAMPLE_INTERVAL);
  ss_adapter.metrics_sample_interval = before;

  /* what userspace asks for; may be out of range */
  g_requested = nd_u16();
  g_handler_rejected = false;

  /* a write, not a read */
  ss_req.newptr = (void *)&g_requested;

  const int error_raw =
    ena_sysctl_metrics_interval(&ss_oid, &ss_adapter, 0, &ss_req);

  const bool applied = ss_adapter.metrics_sample_interval == g_requested;

#ifdef WITH_RC_FIX
  /* The fix is `return (error);` in place of `return (0);` at
   * ena_sysctl.c:931.  The driver source cannot be edited from here, so the
   * repaired return value is reconstructed from the same conditions the
   * handler itself tests: not running, handler failure, or out of range.
   * That is a model of the fix rather than the fix, and is marked as such. */
  const int error =
    (!ENA_FLAG_ISSET(ENA_FLAG_DEVICE_RUNNING, &ss_adapter) ||
     g_handler_rejected || g_requested > ENA_METRICS_MAX_SAMPLE_INTERVAL)
      ? EINVAL
      : error_raw;
#else
  const int error = error_raw;
#endif

  __CPROVER_assert(
    error != 0 || applied,
    "SS_RC_SOUND: the handler returns 0 only if the interval it was asked "
    "for was actually applied");
  __CPROVER_assert(
    error != 0 || !g_handler_rejected,
    "SS_NO_SILENT: the handler does not return 0 on a write it rejected");
  __CPROVER_assert(
    ss_adapter.metrics_sample_interval <= ENA_METRICS_MAX_SAMPLE_INTERVAL,
    "SS_RANGE: an applied interval is within what the driver accepts");

#ifdef VACUITY_CHECK
  /* All must FAIL.  Without them the obligations hold whenever no write is
   * ever rejected, or no interval is ever applied. */
  __CPROVER_assert(
    applied, "witness: a write can be refused and leave the interval alone");
  __CPROVER_assert(
    !applied, "witness: a write can be applied");
  __CPROVER_assert(
    g_requested <= ENA_METRICS_MAX_SAMPLE_INTERVAL,
    "witness: an out-of-range interval can be asked for");
  __CPROVER_assert(
    !g_handler_rejected,
    "witness: the sysctl_handle_16 failure path is reachable");
  /* Both directions, because one alone says little: an assert(X) that fails
   * shows X can be false, not that it can be true.  Getting that backwards
   * is how the flag problem above went unnoticed for a round. */
  __CPROVER_assert(
    ENA_FLAG_ISSET(ENA_FLAG_DEVICE_RUNNING, &ss_adapter),
    "witness: the device-not-running path is reachable");
  __CPROVER_assert(
    !ENA_FLAG_ISSET(ENA_FLAG_DEVICE_RUNNING, &ss_adapter),
    "witness: the device-running path is reachable");
#endif
  return 0;
}
