/// \file
/// tocttou_inode_check_kernel_direct_harness.c — concurrent
/// harness for tocttou_inode_check.
///
/// Two threads:
///
///   * checker thread: captures the current state via
///     __tic_check(), then calls __tic_act(captured).
///   * mutator thread: calls __tic_change(new_state).
///
/// CBMC explores all interleavings.
///
/// ## Vulnerable shape (default)
///
/// The checker holds no lock between check and act, so the
/// mutator can race in between.  CBMC finds an interleaving
/// where __tic_change has already run when __tic_act fires;
/// __tic_act's precondition __tic_state == checked fails.
///
/// ## Safe shape (`-DFIXED`)
///
/// The checker wraps check-and-act in
/// __CPROVER_atomic_begin / _end, modelling a lock held
/// across the sequence.  CBMC's concurrent symex sees the
/// pair as indivisible: the mutator either ran entirely
/// before the check or entirely after the act, and the
/// captured value still matches at the act.

extern unsigned int __tic_state;

unsigned int __tic_check(void);
void __tic_act(unsigned int checked);
void __tic_change(unsigned int new_state);

extern void __CPROVER_atomic_begin(void);
extern void __CPROVER_atomic_end(void);

static void checker(void)
{
#ifndef FIXED
  unsigned int v = __tic_check();
  __tic_act(v);
#else
  __CPROVER_atomic_begin();
  unsigned int v = __tic_check();
  __tic_act(v);
  __CPROVER_atomic_end();
#endif
}

static void mutator(void)
{
  __tic_change(99u);
}

int main(void)
{
  // Initial state.
  __tic_state = 7u;

__CPROVER_ASYNC_1:
  mutator();
  checker();
  return 0;
}
