/// \file
/// cancel_work_before_free.h — property module for "free of a
/// struct containing a work_struct without first cancelling
/// pending work".
///
/// ## Bug class
///
/// A frequent kernel UAF shape: a struct embeds a
/// `work_struct` (or `delayed_work`, `timer_list`); code
/// initialises the work via `INIT_WORK(&x->work, ...)`,
/// schedules it, then frees `x` without first calling
/// `cancel_work_sync(&x->work)`.  The pending work fires after
/// the free and runs against deallocated memory.
///
/// Motivating CVE class: cancel-before-free UAFs in driver and
/// fs subsystems (51 CVEs in the 2023-2026 kernel CVE survey
/// fall under "cleanup_ordering"; many more "use_after_free"
/// CVEs trace back to this shape).
///
/// ## Abstraction
///
/// Per-`work_struct *` ghost flag tracks whether work is
/// pending: set to 1 by `cancel_work_set_pending(work)` (which
/// the harness calls in lockstep with `INIT_WORK` /
/// `__init_work`), cleared by `cancel_work_clear_pending(work)`
/// (called in lockstep with `cancel_work_sync`).
///
/// The `cancel_work_pending(work)` predicate is the assertion
/// the harness checks before each free of a containing struct.
///
/// ## What this module covers
///
/// * **Cocci prefilter**: surfaces every `INIT_WORK` call site
///   and every back-to-back `INIT_WORK ... kfree` shape (no
///   intervening `cancel_work_sync`).
/// * **Direct-call harness**: vuln/fix shapes where the
///   pending-work assertion fires before kfree iff the cancel
///   wasn't done.
///
/// ## What this module does NOT cover (yet)
///
/// * Per-file synthesis on real kernel functions.  The contract
///   on `cancel_work_sync` would have to mutate per-pointer
///   ghost state, which is awkward to express in CBMC contracts
///   (per-pointer tables aren't naturally `__CPROVER_assigns`-
///   able).  Deferred to a follow-up.
/// * Timer / delayed_work variants.  Same shape; left for
///   sibling modules.

#ifndef INTEGRATION_LINUX_PROPERTIES_CANCEL_WORK_BEFORE_FREE_H
#define INTEGRATION_LINUX_PROPERTIES_CANCEL_WORK_BEFORE_FREE_H

struct work_struct;

void cancel_work_set_pending(struct work_struct *work);
void cancel_work_clear_pending(struct work_struct *work);
int cancel_work_pending(struct work_struct *work);

#endif
