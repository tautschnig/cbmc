/// \file
/// kobject_kernel_adapter.c — attaches the kobject_lifetime
/// property module's `kobject_live` predicate as a contract
/// precondition on the kernel's `kobject_put` API.
///
/// ## Why kobject_put?
///
/// `kobject_put` is exported from `lib/kobject.c` (NOT static
/// inline; it is a real EXPORT_SYMBOL function).  Unlike
/// `put_cred` it lives at extern visibility: there is no per-TU
/// mangled `__CPROVER_file_local_*` form to attach to.  The
/// extern-name contract is the only one needed.
///
/// ## The precondition
///
///     __CPROVER_requires(kobj != NULL)
///     __CPROVER_requires(kobject_live(kobj) == 1)
///
/// Translation: at every kobject_put call site the kobject must
/// have a positive ghost usage (i.e. has not already been
/// put-to-zero and freed).  A double put, or a put after a
/// zero-usage init, will fire the precondition.
///
/// ## Struct layout
///
/// `struct kobject` is left as an opaque forward-declaration
/// here (same reasoning as cred_kernel_adapter.c — see
/// LIM-016).  The contract uses pointer identity via
/// `kobject_live(k)`; no field access is needed.

struct kobject;

// ---------------------------------------------------------------------------
// Predicate from the kobject_lifetime property module.
// ---------------------------------------------------------------------------

int kobject_live(struct kobject *k);

// ---------------------------------------------------------------------------
// Contracts.
// ---------------------------------------------------------------------------

// External-name contract.  Parameter name `kobj` must match the
// kernel's <linux/kobject.h> declaration:
//   void kobject_put(struct kobject *kobj);
// (Parameter name mismatch triggers an invariant violation in
// goto-instrument --replace-call-with-contract at contract-
// installation time — see cred_kernel_adapter.c's note for
// detail.)
void kobject_put(struct kobject *kobj)
  __CPROVER_requires(kobj != (struct kobject *)0)
    __CPROVER_requires(kobject_live(kobj) == 1) __CPROVER_assigns();
