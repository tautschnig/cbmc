/// \file
/// rcu_critical_section_per_cpu_concurrent_harness.c —
/// concurrent harness for the per-CPU RCU depth model.
///
/// Three threads (modelled via two __CPROVER_ASYNC_n labels
/// plus main):
///
///   * Thread T1 (reader on CPU 1): rcu_read_lock_t1, do
///     stuff, rcu_read_unlock_t1.
///   * Thread T2 (reader on CPU 2): rcu_read_lock_t2, do
///     stuff, rcu_read_unlock_t2.
///   * Main thread (writer): synchronize_rcu_per_cpu.
///
/// CBMC's concurrent symex explores interleavings.  The
/// vulnerable shape (default) lets the writer fire
/// synchronize_rcu_per_cpu while a reader is in its section;
/// the precondition `__rcu_depth_t1 == 0 && __rcu_depth_t2 ==
/// 0` fails.
///
/// The fix shape (`-DFIXED`) joins the readers (sequentially
/// before the writer's call) so all readers have left their
/// sections before synchronize_rcu_per_cpu runs.

void rcu_read_lock_t1(void);
void rcu_read_unlock_t1(void);
void rcu_read_lock_t2(void);
void rcu_read_unlock_t2(void);
void synchronize_rcu_per_cpu(void);

extern void __CPROVER_atomic_begin(void);
extern void __CPROVER_atomic_end(void);

static void reader_t1(void)
{
  rcu_read_lock_t1();
  // ... read RCU-protected data ...
  rcu_read_unlock_t1();
}

static void reader_t2(void)
{
  rcu_read_lock_t2();
  // ... read RCU-protected data ...
  rcu_read_unlock_t2();
}

int main(void)
{
#ifndef FIXED
  // Vulnerable: writer fires synchronize_rcu_per_cpu
  // concurrently with the readers.  CBMC finds an
  // interleaving where t1 (or t2) is inside its section
  // when synchronize_rcu_per_cpu runs.
__CPROVER_ASYNC_1:
  reader_t1();
__CPROVER_ASYNC_2:
  reader_t2();
  synchronize_rcu_per_cpu();
#else
  // Safe: readers run to completion (sequentially) BEFORE
  // the writer's synchronize_rcu_per_cpu.
  reader_t1();
  reader_t2();
  synchronize_rcu_per_cpu();
#endif

  return 0;
}
