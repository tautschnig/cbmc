/// \file
/// resource_leak_on_error_path.h — property module for
/// "alloc on success path, no free on error path".
///
/// ## Bug class
///
/// A function allocates a resource (memory, file descriptor,
/// reference) on the success path, but an error-exit
/// branch returns without freeing.  Long-lived processes
/// trigger the leak repeatedly, eventually exhausting the
/// resource.
///
/// ```c
/// int handler(...) {
///     void *buf = kmalloc(N, GFP_KERNEL);
///     if (!buf) return -ENOMEM;
///
///     ret = something_that_may_fail();
///     if (ret < 0)
///         return ret;     // BUG: leaks `buf`
///
///     ...
///     kfree(buf);
///     return 0;
/// }
/// ```
///
/// Motivating CVE class: `resource_leak` from the 2023-2026
/// kernel CVE survey — **620 CVEs (7.1% of classified
/// volume).**  Concrete recent examples: CVE-2026-43457
/// (mctp i2c skb leak), CVE-2026-43432 (xhci slot disable),
/// CVE-2026-43445 (e1000 DMA error cleanup).
///
/// ## Abstraction
///
/// Per-pointer ghost flag `alloc_outstanding`.  Set on
/// allocation; cleared on the corresponding free.  Function
/// exits assert that all tracked allocations have been
/// freed.
///
/// ## What this module covers
///
/// * **Direct-call harness** with vuln/fix shapes.
/// * **Cocci prefilter** for "alloc + early-return without
///   free" patterns.
///
/// ## What this module does NOT cover
///
/// * **Per-file synthesis** — the per-file harness can't
///   automatically find every error-exit path; cocci is
///   the primary integration path.
/// * **Allocations through wrappers** (e.g.
///   `devm_kmalloc`, `kvmalloc`).  Cocci can be extended.

#ifndef INTEGRATION_LINUX_PROPERTIES_RESOURCE_LEAK_ON_ERROR_PATH_H
#define INTEGRATION_LINUX_PROPERTIES_RESOURCE_LEAK_ON_ERROR_PATH_H

void leak_alloc_track(const void *p);
void leak_alloc_freed(const void *p);
int leak_outstanding(const void *p);
int leak_any_outstanding(void);

#endif
