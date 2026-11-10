/*
 * Phase 8: concurrency, by named pairs with bounded interleaving.
 *
 * CBMC's thread support does not scale to this driver, so the plan is to take
 * the pairs the DPDK threading model actually allows and interleave their
 * shared accesses by hand.  This harness does the first pair:
 *
 *     the watchdog / control-path thread  vs  ena_stop on the control thread
 *
 * both reaching adapter->trigger_reset and adapter->reset_reason.
 *
 * ena_trigger_reset (ena_ethdev.c:643-650) is an unsynchronised
 * read-modify-write:
 *
 *     if (likely(!adapter->trigger_reset)) {
 *             adapter->reset_reason = reason;
 *             adapter->trigger_reset = true;
 *     }
 *
 * and ena_stop reads the pair (ena_ethdev.c:1382-1386):
 *
 *     if (adapter->trigger_reset)
 *             rc = ena_com_dev_reset(ena_dev, adapter->reset_reason);
 *
 * There is no lock and no barrier between the two stores.  The questions:
 *
 *   CC_RESET_NOT_LOST  if any requester asked for a reset, the flag is set --
 *                      a concurrent request must not be swallowed
 *   CC_REASON_SOUND    a reader that sees the flag set reads a reason that
 *                      some requester actually asked for, never the
 *                      pre-existing value
 *
 * The two store orders a compiler or a weakly-ordered CPU may produce absent
 * barriers are modelled explicitly (STORE_ORDER), rather than relying on
 * CBMC's sequentially-consistent default, which would hide the question.
 *
 * Switches: STORE_ORDER=0 (reason then flag, as written), 1 (flag then
 * reason, the reordering the absent barrier permits), VACUITY_CHECK.
 */
#include <stdbool.h>
#include <stdint.h>

#ifndef STORE_ORDER
#define STORE_ORDER 0
#endif

enum reset_reason
{
  REASON_NONE = 0,
  REASON_KEEP_ALIVE = 1, /* the watchdog's */
  REASON_TX_CMPL = 2,    /* the datapath's */
};

struct adapter
{
  bool trigger_reset;
  enum reset_reason reset_reason;
};

static int nd_int(void) { int v; return v; }

/*
 * ena_trigger_reset, split into the three steps a reader can interleave
 * between: the guard load, the reason store and the flag store.  Executing
 * the whole function in one step -- which an earlier version of this harness
 * did -- makes it atomic and hides exactly the window under test: both
 * obligations then hold even in the reordered variant, which is how the flaw
 * was noticed.
 */
struct requester
{
  enum reset_reason reason;
  unsigned pc;   /* 0 = not started, 1 = guard passed, 2 = first store done */
  bool finished;
};

static void requester_step(struct adapter *a, struct requester *r)
{
  if(r->finished)
    return;
  if(r->pc == 0)
  {
    if(a->trigger_reset)
    {
      r->finished = true; /* the guard sent it away */
      return;
    }
    r->pc = 1;
    return;
  }
  if(r->pc == 1)
  {
#if STORE_ORDER == 0
    a->reset_reason = r->reason;
#else
    a->trigger_reset = true; /* the reordering nothing in the source forbids */
#endif
    r->pc = 2;
    return;
  }
#if STORE_ORDER == 0
  a->trigger_reset = true;
#else
  a->reset_reason = r->reason;
#endif
  r->finished = true;
}

int main(void)
{
  struct adapter a;
  a.trigger_reset = false;
  a.reset_reason = REASON_NONE;

  /*
   * Two requesters and one reader, interleaved at every point where one of
   * them touches the shared pair.  Each step is chosen nondeterministically,
   * so every interleaving of the three participants' accesses is covered up
   * to this bound.
   */
  struct requester wd = {REASON_KEEP_ALIVE, 0, false};
  struct requester dp = {REASON_TX_CMPL, 0, false};
  bool asked = false;
  bool observed_flag = false;
  enum reset_reason observed_reason = REASON_NONE;

  for(unsigned step = 0; step < 7; step++)
  {
    const int who = nd_int();
    if(who == 0)
      requester_step(&a, &wd);
    else if(who == 1)
      requester_step(&a, &dp);
    else if(who == 2)
    {
      /* ena_stop's read of the pair, which is itself two separate loads */
      observed_flag = a.trigger_reset;
      if(observed_flag)
        observed_reason = a.reset_reason;
    }
  }

  /* A requester "asked" only once it has run to completion; one that is still
   * mid-sequence when the step budget runs out has not, and counting it would
   * make CC_RESET_NOT_LOST fail for a bookkeeping reason rather than a real
   * one (an earlier version of this harness did exactly that). */
  asked = (wd.finished && wd.pc >= 1) || (dp.finished && dp.pc >= 1);

  /* ---- obligations ---- */
  __CPROVER_assert(
    !asked || a.trigger_reset,
    "CC_RESET_NOT_LOST: a requested reset is never swallowed by a concurrent "
    "request");
  __CPROVER_assert(
    !observed_flag || observed_reason == REASON_KEEP_ALIVE ||
      observed_reason == REASON_TX_CMPL,
    "CC_REASON_SOUND: a reader that sees the flag set reads a reason some "
    "requester asked for");

#ifdef VACUITY_CHECK
  __CPROVER_assert(!a.trigger_reset, "vacuity witness: a reset can be requested");
  __CPROVER_assert(!observed_flag, "vacuity witness: the reader can see it set");
  __CPROVER_assert(
    observed_reason != REASON_TX_CMPL,
    "vacuity witness: either requester's reason can be observed");
#endif
  return 0;
}
