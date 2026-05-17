/// \file
/// kref_kernel_adapter.c — attaches the
/// kref_lifetime property module's `kref_live`
/// predicate as a contract precondition on the kernel's
/// `kref_put` API.

struct kref;

int kref_live(struct kref *kref);

// Contract on the mangled static-inline form.  Parameter name
// `kref` must match the kernel's
// `<linux/kref.h>` declaration; mismatch triggers an
// invariant violation in goto-instrument
// --replace-call-with-contract at contract-installation time.
int __CPROVER_file_local_kref_h_kref_put(struct kref *kref, void (*release)(struct kref *kref))
  __CPROVER_requires(kref != (struct kref *)0)
  __CPROVER_requires(kref_live(kref) == 1)
  __CPROVER_assigns();


// External-name contract for direct-call harness links and any
// kernel TU that resolves the call to the external symbol.
int kref_put(struct kref *kref, void (*release)(struct kref *kref))
  __CPROVER_requires(kref != (struct kref *)0)
  __CPROVER_requires(kref_live(kref) == 1)
  __CPROVER_assigns();
