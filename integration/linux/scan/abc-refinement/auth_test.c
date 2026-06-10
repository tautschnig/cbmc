// Authorization threat template (A5).  CBMC obligation:
//   asset   = authorization (a privileged action requires a capability)
//   actor   = an unprivileged task invoking the handler
//   threat  = the privileged action runs without the capability held
//   mitigation to prove = on EVERY path to the action, capable() held
//
// Modelled: `g_cap_held` is whether the calling task actually holds the
// capability (nondet -- the attacker may not).  capable() reports it.  The
// privileged action asserts the capability is held -- so the obligation is
// discharged exactly when a capable() check dominates the action (the same
// dominance relation caller_precondition uses for length guards, now over a
// capability guard).
#include <stdint.h>

int g_cap_held;

int nd(void)
{
  int x;
  return x;
}

// models capable(CAP_*) -- returns whether the task holds it
static int capable_model(int cap)
{
  (void)cap;
  return g_cap_held;
}

// the privileged, state-changing action
static void privileged_action(void)
{
  __CPROVER_assert(g_cap_held, "privileged action requires the capability");
}

// BUGGY: no capability check before the privileged action.
int handler_buggy(int cap)
{
  g_cap_held = nd();
  (void)cap;
  privileged_action(); // reached even when !g_cap_held -> FAILED
  return 0;
}

// FIXED: a dominating capable() check rejects the unprivileged caller.
int handler_fixed(int cap)
{
  g_cap_held = nd();
  if(!capable_model(cap))
    return -1;        // -EPERM
  privileged_action(); // reached only when g_cap_held -> SUCCESS
  return 0;
}
