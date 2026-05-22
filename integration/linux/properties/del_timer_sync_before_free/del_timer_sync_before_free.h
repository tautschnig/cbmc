/// \file
/// del_timer_sync_before_free.h — sibling of
/// cancel_work_before_free for `struct timer_list`.
///
/// ## Bug class
///
/// ```c
/// struct foo *x = kmalloc(sizeof(*x), GFP_KERNEL);
/// timer_setup(&x->t, callback, 0);
/// mod_timer(&x->t, jiffies + HZ);
/// ...
/// kfree(x);   // BUG: timer fires against freed memory
/// ```
///
/// The fix is `del_timer_sync(&x->t)` (or
/// `timer_delete_sync(&x->t)` in 6.x rename) before the kfree.
///
/// Motivating CVE class: same `cleanup_ordering` /
/// `use_after_free` buckets as cancel_work_before_free.
///
/// ## Abstraction
///
/// Per-`struct timer_list *` ghost flag tracks whether a
/// timer is armed.

#ifndef INTEGRATION_LINUX_PROPERTIES_DEL_TIMER_SYNC_BEFORE_FREE_H
#define INTEGRATION_LINUX_PROPERTIES_DEL_TIMER_SYNC_BEFORE_FREE_H

struct timer_list;

void timer_set_armed(struct timer_list *t);
void timer_clear_armed(struct timer_list *t);
int timer_armed(struct timer_list *t);

#endif
