/// \file
/// concurrent_double_put.h — property module for kernel
/// "concurrent double put" race bugs.
///
/// ## Bug class
///
/// Two threads believe they each hold a reference to the same
/// shared object and concurrently call its put-API.  Without
/// proper synchronisation, both reads of the live state see
/// "still live" before either decrement runs; one ordering of
/// the puts ends up calling put on an already-zero refcount,
/// freeing memory the other thread still holds.
///
/// ```c
/// // Thread A:                  // Thread B:
/// if (cred_live(c)) {            if (cred_live(c)) {
///     put_cred(c);                   put_cred(c);     // BUG
/// }                              }
/// ```
///
/// The kernel's atomic refcount API (`refcount_dec_and_test`)
/// makes this hard to hit on properly-instrumented types, but
/// it still appears in the CVE record where the put-API uses
/// non-atomic state, or where the refcount is checked-then-put
/// without atomic decrement.
///
/// Motivating CVE class: `race_or_toctoue` from the 2023-2026
/// kernel CVE survey (232 CVEs), specifically the subset
/// involving refcount races.  Concrete recent examples:
/// CVE-2026-43322 (Bluetooth UAF in
/// le_read_features_complete), various AF_VSOCK / sock_put
/// races.
///
/// ## Abstraction
///
/// One integer ghost `__cdp_live` (1 = live, 0 = dead) for the
/// shared object.  Two contracts:
///
///   * `cdp_get`   — assigns + ensures `__cdp_live = 1`.
///   * `cdp_put`   — requires `__cdp_live == 1`; assigns +
///                   ensures `__cdp_live = 0`.
///
/// CBMC's concurrent symex explores all interleavings of two
/// writer threads each calling `cdp_put`.  The bug shape is
/// detected when both threads have passed their
/// `cdp_is_live()` check before either's `cdp_put` runs;
/// CBMC finds an interleaving where the second `cdp_put`'s
/// precondition fires.
///
/// ## What this module covers
///
/// * **Direct-call concurrent harness** with vuln/fix shapes
///   exercising the race.
/// * **Cocci prefilter** flagging unprotected
///   "if-live-then-put" patterns.
///
/// ## What this module does NOT cover
///
/// * **Atomic refcount races on `refcount_t`** specifically —
///   these are caught by the existing `refcount_lifetime` module
///   sequentially.  This module addresses races involving
///   non-atomic state or check-then-act patterns.
/// * **Per-file synthesis** — concurrent execution doesn't
///   fit the per-file model directly.

#ifndef INTEGRATION_LINUX_PROPERTIES_CONCURRENT_DOUBLE_PUT_H
#define INTEGRATION_LINUX_PROPERTIES_CONCURRENT_DOUBLE_PUT_H

extern unsigned int __cdp_live;

void cdp_get(void);
void cdp_put(void);
void cdp_clear(void);

int cdp_is_live(void);

#endif
