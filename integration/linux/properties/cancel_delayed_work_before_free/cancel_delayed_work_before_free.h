/// \file
/// cancel_delayed_work_before_free.h — sibling of
/// cancel_work_before_free for `struct delayed_work` (the
/// timer-driven work-queue variant).
///
/// ## Bug class
///
/// Same shape as cancel_work_before_free but with
/// `struct delayed_work` and `cancel_delayed_work_sync`:
///
/// ```c
/// struct foo *x = kmalloc(sizeof(*x), GFP_KERNEL);
/// INIT_DELAYED_WORK(&x->dwork, worker);
/// schedule_delayed_work(&x->dwork, HZ);
/// ...
/// kfree(x);   // BUG: pending delayed work runs against freed memory
/// ```
///
/// The fix is `cancel_delayed_work_sync(&x->dwork)` before
/// the kfree.
///
/// Motivating CVE class: same `cleanup_ordering` /
/// `use_after_free` buckets as cancel_work_before_free.
/// Concrete examples: many driver / fs CVEs involve
/// delayed_work on long-lived structures.
///
/// ## Abstraction
///
/// Per-`struct delayed_work *` ghost flag tracks whether
/// delayed work is pending: set by
/// `cancel_dwork_set_pending`, cleared by
/// `cancel_dwork_clear_pending`, queried by
/// `cancel_dwork_pending`.
///
/// Mirrors cancel_work_before_free's API exactly with a
/// distinct ghost table keyed by `struct delayed_work *`
/// rather than `struct work_struct *`.

#ifndef INTEGRATION_LINUX_PROPERTIES_CANCEL_DELAYED_WORK_BEFORE_FREE_H
#define INTEGRATION_LINUX_PROPERTIES_CANCEL_DELAYED_WORK_BEFORE_FREE_H

struct delayed_work;

void cancel_dwork_set_pending(struct delayed_work *dwork);
void cancel_dwork_clear_pending(struct delayed_work *dwork);
int cancel_dwork_pending(struct delayed_work *dwork);

#endif
