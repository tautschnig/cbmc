/// \file
/// cancel_work_before_free_kernel_adapter.c
///
/// __assert_no_pending_work(work) is a synthetic checkpoint
/// inserted by Coccinelle instrumentation (and the per-return
/// fallback) before each kfree of a struct that embeds a
/// work_struct.  This file declares its CBMC contract AND
/// provides an empty body so that linking succeeds and
/// goto-instrument can apply --replace-call-with-contract to it.

struct work_struct;
int cancel_work_pending(struct work_struct *work);

void __assert_no_pending_work(struct work_struct *work)
  __CPROVER_requires(cancel_work_pending(work) == 0) __CPROVER_assigns()
{
  (void)work;
}
