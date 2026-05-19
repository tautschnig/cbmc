/// \file
/// concurrent_pointer_publish_kernel_direct_harness.c —
/// CBMC concurrent harness for the
/// concurrent_pointer_publish property module.
///
/// Two threads (using CBMC's __CPROVER_ASYNC_n labels for
/// thread spawn):
///
///   * writer: simulates the kernel producer that publishes
///             a "ready" flag and writes data.
///   * reader: simulates a concurrent consumer that checks
///             the flag and reads data.
///
/// In the vulnerable shape (default), the writer publishes
/// FIRST, then initialises.  CBMC's concurrent symex finds an
/// interleaving where the reader's assertion fires.
///
/// In the safe shape (`-DFIXED`), the writer initialises
/// first and only then publishes.  The reader's assertion
/// always holds.
///
/// **Pointer-concurrency note.**  CBMC's pointer-concurrency
/// model is unsound (it warns "pointer handling for
/// concurrency is unsound" and refuses to prove anything).
/// To stay sound we model the published / initialised state
/// with two integer flags rather than a pointer.  The bug
/// shape we want to surface is a write-ordering question,
/// faithful regardless of the value's type.

extern void cpp_publish(void);
extern void cpp_initialise(void);
extern void cpp_clear(void);
extern int cpp_is_published(void);
extern void __cpp_assert_safe_to_read(void);

static void writer(void)
{
#ifndef FIXED
  // Vulnerable order: publish before init.  Concurrent
  // reader can witness published=1 with initialised=0.
  cpp_publish();
  cpp_initialise();
#else
  // Safe order: init then publish.
  cpp_initialise();
  cpp_publish();
#endif
}

static void reader(void)
{
  if(cpp_is_published())
  {
    // The contract on __cpp_assert_safe_to_read fires unless
    // initialised has already been set.
    __cpp_assert_safe_to_read();
  }
}

int main(void)
{
  cpp_clear();

  // CBMC pragma: spawn `writer` as an async thread.  The
  // remainder of `main` runs concurrently as the second
  // thread.
__CPROVER_ASYNC_1:
  writer();
  reader();
  return 0;
}
