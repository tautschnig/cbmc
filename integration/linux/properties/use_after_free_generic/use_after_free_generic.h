/// \file
/// use_after_free_generic.h — property module for explicit
/// free-then-use AND double-free bug shapes (NON-refcount).
///
/// ## Bug class
///
/// Two related shapes that are NOT covered by the balance
/// modules (cred_lifetime / kobject_lifetime / etc., which
/// catch refcount-imbalance UAFs):
///
/// 1. **Explicit free-then-use:**
///    ```c
///    kfree(p);
///    p->field = 42;   // BUG: dereference of freed pointer
///    ```
///
/// 2. **Double-free:**
///    ```c
///    kfree(p);
///    kfree(p);        // BUG: kfree of already-freed pointer
///    ```
///
/// Motivating CVE class: `use_after_free` from the 2023-2026
/// kernel CVE survey (899 CVEs, 10.3% — the third-largest
/// category) **excluding the refcount-imbalance subset** that
/// the balance modules already cover.  Net new coverage from
/// this module: an estimated 5-7% of survey volume.
///
/// Plus `double_free_or_unlock` (109 CVEs, 1.3%) — most
/// kfree-double-frees fall here.
///
/// ## Abstraction
///
/// Per-pointer ghost flag `freed`.  Set by `kfree` (modelled
/// via the harness's `mark_freed` helper); cleared by a fresh
/// allocation that returns the same pointer (rare).  Two
/// contracts:
///
///   * `__assert_not_freed(p)` — required before any deref.
///   * `__assert_not_double_free(p)` — required at every
///     subsequent kfree.

#ifndef INTEGRATION_LINUX_PROPERTIES_USE_AFTER_FREE_GENERIC_H
#define INTEGRATION_LINUX_PROPERTIES_USE_AFTER_FREE_GENERIC_H

void mark_freed(const void *p);
void mark_alive(const void *p);
int is_freed(const void *p);

#endif
