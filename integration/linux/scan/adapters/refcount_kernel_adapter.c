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
/// path).  It's defined as an ordinary extern in
/// `<linux/refcount.h>` (not static inline), so unlike
/// put_cred / pipe_buf_release there is no file-local-mangled
/// form — the external symbol suffices.
///
/// ## The precondition
///
///     __CPROVER_requires(r != NULL)
///     __CPROVER_requires(refcount_live(r) == 1)
///
/// Translation: at every `refcount_dec_and_test` call site the
/// counter must have positive usage (i.e. was not already
/// decremented to zero and the object not already freed).  A
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

// Contract on the external symbol.  Parameter name `r` matches
// the kernel's canonical declaration in <linux/refcount.h>:
//   bool refcount_dec_and_test(refcount_t *r);
// (CBMC treats parameter names as part of the code_with_contract
// type equality.)
_Bool refcount_dec_and_test(refcount_t *r)
  __CPROVER_requires(r != (refcount_t *)0)
    __CPROVER_requires(refcount_live(r) == 1) __CPROVER_assigns();
