/// \file
/// refcount_kernel_adapter.c — attaches the refcount_lifetime
/// property module's `refcount_live` predicate as a contract
/// precondition on the kernel's `refcount_dec_and_test` API.
///
/// ## Why refcount_dec_and_test?
///
/// `refcount_dec_and_test` is the canonical kernel API for
/// dropping a `refcount_t`; the return value indicates whether
/// the counter reached zero (triggering the object's free
/// path).
///
/// ## Static-inline mangling: three contract declarations
///
/// In modern kernels `refcount_dec_and_test` is
/// `static inline __must_check`, and it's a thin wrapper
/// around `__refcount_dec_and_test` which is also static
/// inline.  Under `goto-cc --export-file-local-symbols`, each
/// kernel TU that includes `<linux/refcount.h>` exposes both
/// functions under mangled file-local names:
///
///   refcount_dec_and_test →
///       __CPROVER_file_local_refcount_h_refcount_dec_and_test
///   __refcount_dec_and_test →
///       __CPROVER_file_local_refcount_h___refcount_dec_and_test
///
/// Kernel call sites inside a .c file use the mangled name.
/// For the scan's `goto-instrument --replace-call-with-contract`
/// pass to attach the precondition at those sites, the contract
/// MUST be declared under the mangled name as well.  Without
/// this, the contract only fires at harness call sites (which
/// use the external non-static declaration), and real kernel
/// call sites pass through the scan unchecked.
///
/// We declare the contract on all three forms: the external
/// (for direct-call harness links) and the two mangled forms
/// (for kernel-TU links).  One typically binds per link.
///
/// ## The precondition
///
///     __CPROVER_requires(r != NULL)
///     __CPROVER_requires(refcount_live(r) == 1)
///
/// Translation: the counter must have positive usage.  A
/// double-dec, or a dec after a zero-usage init, fires the
/// precondition.

typedef struct refcount_struct refcount_t;

// ---------------------------------------------------------------------------
// Predicate from the refcount_lifetime property module.
// ---------------------------------------------------------------------------

int refcount_live(refcount_t *r);

// ---------------------------------------------------------------------------
// Contracts.
// ---------------------------------------------------------------------------

// External name (direct-call harness link; some non-static
// kernel build configurations also export it).
_Bool refcount_dec_and_test(refcount_t *r)
  __CPROVER_requires(r != (refcount_t *)0)
    __CPROVER_requires(refcount_live(r) == 1) __CPROVER_assigns();

// File-local-mangled form as emitted by goto-cc on every
// kernel TU that includes <linux/refcount.h>.  This is the
// form that real kernel call sites use.
_Bool __CPROVER_file_local_refcount_h_refcount_dec_and_test(refcount_t *r)
  __CPROVER_requires(r != (refcount_t *)0)
    __CPROVER_requires(refcount_live(r) == 1) __CPROVER_assigns();

// File-local-mangled form of __refcount_dec_and_test, the
// static inline helper that refcount_dec_and_test wraps.  If
// a kernel TU happens to call __refcount_dec_and_test
// directly (some paths do, e.g. to pass back the oldp
// out-parameter), the contract needs to fire there too.  The
// function signature is different — it takes a second int *
// parameter — but the precondition on r is the same.
_Bool __CPROVER_file_local_refcount_h___refcount_dec_and_test(
  refcount_t *r,
  int *oldp) __CPROVER_requires(r != (refcount_t *)0)
  __CPROVER_requires(refcount_live(r) == 1) __CPROVER_assigns();
