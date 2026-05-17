/// \file
/// rcu_critical_section_kernel_direct_harness.c — direct-call
/// harness for the rcu_critical_section property module.
///
/// Two test paths under one binary, controlled by `-DFIXED`.
///
/// ### Vulnerable shape (default)
///
///   1. `rcu_read_lock()`   — depth 0 -> 1.
///   2. `synchronize_rcu()` — REQUIRES depth == 0; FIRES because
///      we're inside the critical section.
///   3. `rcu_read_unlock()` — depth 1 -> 0.
///
/// ### Safe shape (`-DFIXED`)
///
///   1. `rcu_read_lock()`   — depth 0 -> 1.
///   2. `rcu_read_unlock()` — depth 1 -> 0.
///   3. `synchronize_rcu()` — depth == 0; precondition holds.

void rcu_read_lock(void);
void rcu_read_unlock(void);
void synchronize_rcu(void);

int main(void)
{
  rcu_read_lock();

#ifndef FIXED
  // Sleeping operation INSIDE an RCU critical section — bug.
  synchronize_rcu();
  rcu_read_unlock();
#else
  // Safe order: leave the critical section first, then sleep.
  rcu_read_unlock();
  synchronize_rcu();
#endif

  return 0;
}
