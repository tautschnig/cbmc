/// \file
/// concurrent_pointer_publish.h — property module for kernel
/// "publication-before-initialisation" race bugs.
///
/// ## Bug class
///
/// A producer thread initialises a shared object's "valid"
/// (or "ready", "active", or similar) flag before its data
/// payload, exposing a window in which a concurrent reader
/// sees the flag set but reads stale or uninitialised data:
///
/// ```c
/// // Writer thread (BUG ORDER):
/// shared->ready = 1;     // publish
/// shared->data  = 42;    // initialise after
///
/// // Reader thread:
/// if (shared->ready) {
///     int v = shared->data;   // may read 0 — uninitialised
/// }
/// ```
///
/// The fix is to flip the order (init then publish) and use
/// `smp_store_release()` / `smp_load_acquire()`, or to use
/// `rcu_assign_pointer()` for pointer publication (but
/// **pointer publication is intentionally NOT covered by this
/// module** — see "Limitations" below).
///
/// Motivating CVE class: the `race_or_toctoue` bucket from
/// the 2023-2026 kernel CVE survey (232 CVEs, 2.7% of
/// classified volume).  Concrete recent examples in this
/// shape: CVE-2026-43420 (ceph i_nlink underrun during async
/// unlink), CVE-2026-43439 (cgroup race between task migration
/// and iteration), and the general AF_VSOCK / Bluetooth /
/// netfilter race family.
///
/// ## Abstraction
///
/// Two integer ghosts model the published / initialised
/// state of one synthetic "shared object slot":
///
///   * `__cpp_published`  — non-zero iff publish has run.
///   * `__cpp_initialised` — non-zero iff init has run.
///
/// CBMC's concurrent symex explores all interleavings of the
/// writer's `publish` and `init` calls relative to the
/// reader's checks.  Bug shape: writer order
/// `publish; init;` lets the reader see `published == 1 &&
/// initialised == 0`, which the contract on the read API
/// rejects.
///
/// **Why integers, not pointers?** CBMC's pointer-concurrency
/// model is unsound (it warns "pointer handling for
/// concurrency is unsound" and refuses to prove anything).
/// Using integer flags sidesteps the unsoundness — the bug
/// shape is faithfully captured because the kernel's actual
/// pointer publication boils down to a flag-write-and-read
/// ordering question regardless of the value's type.
///
/// ## What this module covers
///
/// * **Direct-call harness** with vuln/fix shapes that
///   exercise CBMC's concurrent execution.
/// * **Cocci prefilter** flagging suspicious "ready before
///   data" assignment orders.
///
/// ## What this module does NOT cover
///
/// * **Real pointer publication** — `rcu_assign_pointer`,
///   `WRITE_ONCE` on pointer fields.  CBMC's concurrent
///   pointer model is unsound, so we'd produce vacuous
///   verdicts.  The kernel idiom of `rcu_assign_pointer(p,
///   new)` after `new->field = ...` is captured by this
///   module's flag-pair abstraction; analysing the literal
///   pointer is out of scope.
///
/// * **Memory-ordering subtleties** beyond "happens-before".
///   We model sequential consistency.  Real ARM/POWER weak
///   memory models can expose additional bugs that this
///   module does not surface.
///
/// * **Multi-thread cross-CPU race scenarios** with more than
///   two threads.  The harness has one writer + one reader.

#ifndef INTEGRATION_LINUX_PROPERTIES_CONCURRENT_POINTER_PUBLISH_H
#define INTEGRATION_LINUX_PROPERTIES_CONCURRENT_POINTER_PUBLISH_H

extern unsigned int __cpp_published;
extern unsigned int __cpp_initialised;

// State-mutator helpers for the writer side.  In the kernel,
// these correspond to `WRITE_ONCE(x->ready, 1)` / data writes.
void cpp_publish(void);
void cpp_initialise(void);
void cpp_clear(void);

// Predicates for the reader side / contract preconditions.
int cpp_is_published(void);
int cpp_is_initialised(void);
int cpp_safe_to_read(void);

#endif
