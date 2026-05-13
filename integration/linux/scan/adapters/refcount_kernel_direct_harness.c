/// \file
/// refcount_kernel_direct_harness.c — direct-call harness for
/// the refcount_lifetime property module.
///
/// Mirrors cred_kernel_direct_harness.c's structure: build a
/// refcount_t sentinel, register it with the ghost table, and
/// call the contract target (`refcount_dec_and_test`) in a
/// vulnerable-or-safe shape selected by `-DFIXED`.  Because
/// --replace-call-with-contract replaces the body of
/// refcount_dec_and_test with only the contract (no ghost
/// update), the harness explicitly updates the
/// refcount_lifetime ghost alongside each call — this mirrors
/// the real kernel's refcount_dec_and_test body which
/// atomically decrements.
///
/// ### Vulnerable harness
///
///   1. A refcount_t is initialised with `usage = 1`.
///   2. `refcount_dec_and_test` is called — the contract
///      precondition holds because the counter is still live.
///      After the call, the harness manually drops the ghost
///      usage to zero.
///   3. `refcount_dec_and_test` is called a second time.  The
///      contract precondition `refcount_live(r) == 1` must
///      FIRE: the counter is no longer live.
///
/// ### Safe harness (-DFIXED)
///
/// Step 1 initialises `usage = 2`.  After the first dec the
/// harness drops usage to 1; both calls therefore see a live
/// counter.

typedef unsigned long size_t;

typedef struct refcount_struct refcount_t;

// Ghost-state API from the property module.
void refcount_lifetime_init(refcount_t *r, unsigned int usage);
void refcount_lifetime_inc(refcount_t *r);
int refcount_lifetime_dec_and_test(refcount_t *r);

// Contract target (declared by the adapter).
_Bool refcount_dec_and_test(refcount_t *r);

int main(void)
{
  // Backing buffer larger than any plausible kernel
  // refcount_t (struct refcount_struct { atomic_t refs; } — 4
  // bytes — plus alignment).  The content is irrelevant;
  // only the address is used as a ghost-table key.
  static char refcount_sentinel[64];
  refcount_t *r = (refcount_t *)refcount_sentinel;

#ifndef FIXED
  // Vulnerable: initial usage = 1.  First dec drops to 0;
  // second dec fires the refcount_live precondition.
  refcount_lifetime_init(r, 1);
#else
  // Safe: initial usage = 2.  Both decs land on a still-live
  // counter and the precondition holds throughout.
  refcount_lifetime_init(r, 2);
#endif

  (void)refcount_dec_and_test(r);
  // Mirror the real refcount_dec_and_test's side effect on the
  // ghost.  The contract replaces the body so the ghost isn't
  // updated by refcount_dec_and_test itself.
  (void)refcount_lifetime_dec_and_test(r);

  (void)refcount_dec_and_test(r);
  (void)refcount_lifetime_dec_and_test(r);

  return 0;
}
