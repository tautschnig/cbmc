/// \file
/// dentry_kernel_adapter.c — attaches the
/// dentry_lifetime property module's `dentry_live`
/// predicate as a contract precondition on the kernel's
/// `dput` API.

struct dentry;

int dentry_live(struct dentry *dentry);

// External-name contract for direct-call harness links and any
// kernel TU that resolves the call to the external symbol.
void dput(struct dentry *dentry)
  __CPROVER_requires(dentry != (struct dentry *)0)
  __CPROVER_requires(dentry_live(dentry) == 1)
  __CPROVER_assigns();
