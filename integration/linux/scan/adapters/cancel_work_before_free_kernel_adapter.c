/// \file
/// cancel_work_before_free_kernel_adapter.c — adapter for the
/// cancel_work_before_free property module.
///
/// Unlike the balance modules (which contract a kernel API
/// directly), this module contracts a synthetic
/// `__assert_no_pending_work` checkpoint function that the
/// direct-call harness inserts before each free.  The contract
/// requires `cancel_work_pending(work) == 0` at the checkpoint;
/// CBMC discharges the precondition iff the harness called
/// `cancel_work_clear_pending(work)` before the checkpoint.
///
/// Real-kernel per-file synthesis is not yet supported by this
/// module — see the property-module README's "What this module
/// does NOT cover" section.  Cocci-driven candidate surfacing
/// is the primary integration path until a follow-up extends
/// per-file synthesis to wire the harness's checkpoint into
/// real kfree call sites automatically.

struct work_struct;

int cancel_work_pending(struct work_struct *work);

void __assert_no_pending_work(struct work_struct *work)
  __CPROVER_requires(cancel_work_pending(work) == 0)
  __CPROVER_assigns();
