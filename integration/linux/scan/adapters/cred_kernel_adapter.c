/// \file
/// cred_kernel_adapter.c — attaches the cred_lifetime property
/// module's `cred_live` predicate as a contract precondition on
/// the kernel's `put_cred` API.
///
/// ## Why put_cred?
///
/// `put_cred` is `static inline` in `<linux/cred.h>` and called
/// from thousands of kernel sites.  Under
/// `goto-cc --export-file-local-symbols` it gets a mangled name
/// `__CPROVER_file_local_cred_h_put_cred` in every kernel TU
/// that references it.  Attaching the contract to both the
/// unmangled and the mangled forms gives the scan a target that
/// is reliably present in linked kernel binaries and maps
/// one-to-one onto the cred_lifetime invariant: every put_cred
/// must happen on a still-live cred.
///
/// ## The precondition
///
///     __CPROVER_requires(cred != NULL)
///     __CPROVER_requires(cred_live(cred) == 1)
///
/// Translation: at every put_cred call site the cred must have a
/// positive usage (i.e. has not already been freed).  A double
/// put, or a put after a zero-usage init, will fire the
/// precondition.
///
/// ## Struct layout
///
/// We re-declare `struct cred` with just the `usage` field
/// (matching the kernel's first-field layout) plus enough padding
/// that taking pointers stays compatible with the kernel TU.

struct cred
{
  // Matches `atomic_t usage` — modeled as unsigned int for
  // abstract reasoning.
  unsigned int usage;
  unsigned long _pad;
};

// ---------------------------------------------------------------------------
// Predicate from the cred_lifetime property module.
// ---------------------------------------------------------------------------

int cred_live(struct cred *c);

// ---------------------------------------------------------------------------
// Contracts.
// ---------------------------------------------------------------------------

// Contract on the mangled static-inline form.
void __CPROVER_file_local_cred_h_put_cred(struct cred *cred)
  __CPROVER_requires(cred != (struct cred *)0)
    __CPROVER_requires(cred_live(cred) == 1) __CPROVER_assigns();

// External-name contract for direct-call harness links.
void put_cred(struct cred *cred) __CPROVER_requires(cred != (struct cred *)0)
  __CPROVER_requires(cred_live(cred) == 1) __CPROVER_assigns();
