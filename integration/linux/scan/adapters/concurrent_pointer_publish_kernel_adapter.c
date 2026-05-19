/// \file
/// concurrent_pointer_publish_kernel_adapter.c — adapter for
/// the concurrent_pointer_publish module.
///
/// Unlike the balance modules, this module's primary
/// integration is a CBMC concurrent harness rather than a
/// per-API contract on a kernel function.  The "contract" is
/// the assertion the harness embeds: the reader's
/// `cpp_safe_to_read()` predicate must hold whenever
/// `cpp_is_published()` does.
///
/// We attach a contract to a synthetic checkpoint function
/// `__cpp_assert_safe_to_read()` that the direct-call harness
/// calls in the reader thread.  The contract requires
/// `cpp_safe_to_read() == 1`; CBMC discharges this iff the
/// writer thread has already called `cpp_initialise()` before
/// the reader's check.  Bug shapes (publish-before-init) leave
/// a window where the reader fires the precondition.

extern int cpp_safe_to_read(void);

void __cpp_assert_safe_to_read(void) __CPROVER_requires(cpp_safe_to_read() == 1)
  __CPROVER_assigns();
