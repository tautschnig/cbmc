/// \file
/// concurrent_double_put_kernel_direct_harness.c — concurrent
/// harness for the concurrent_double_put property module.
///
/// Two threads (using CBMC's __CPROVER_ASYNC_n labels) each
/// run a "check then put" sequence on the same shared object.
///
/// ## Vulnerable shape (default)
///
///   Thread A:                   Thread B:
///     if (cdp_is_live())          if (cdp_is_live())
///       __cdp_put();                __cdp_put();
///
///   CBMC's concurrent symex finds an interleaving where both
///   threads pass their is_live check before either's put runs.
///   The second put's precondition __cdp_live == 1 fires.
///
/// ## Safe shape (`-DFIXED`)
///
///   Each thread does an atomic check-and-decrement using an
///   __CPROVER_atomic_begin / _end block.  CBMC sees the
///   sequence as indivisible; the second thread either has
///   already seen live=0 (and skips) or hasn't yet checked.

#include "../../properties/concurrent_double_put/concurrent_double_put.h"

void __cdp_get(void);
void __cdp_put(void);

extern void __CPROVER_atomic_begin(void);
extern void __CPROVER_atomic_end(void);

static void thread_a(void)
{
#ifndef FIXED
  if(cdp_is_live())
  {
    __cdp_put();
  }
#else
  __CPROVER_atomic_begin();
  if(cdp_is_live())
  {
    __cdp_put();
  }
  __CPROVER_atomic_end();
#endif
}

static void thread_b(void)
{
#ifndef FIXED
  if(cdp_is_live())
  {
    __cdp_put();
  }
#else
  __CPROVER_atomic_begin();
  if(cdp_is_live())
  {
    __cdp_put();
  }
  __CPROVER_atomic_end();
#endif
}

int main(void)
{
  cdp_clear();
  __cdp_get();

__CPROVER_ASYNC_1:
  thread_a();
  thread_b();
  return 0;
}
