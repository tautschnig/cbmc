/// \file
/// inode_kernel_adapter.c — attaches the
/// inode_lifetime property module's `inode_live`
/// predicate as a contract precondition on the kernel's
/// `iput` API.

struct inode;

int inode_live(struct inode *inode);

// External-name contract for direct-call harness links and any
// kernel TU that resolves the call to the external symbol.
void iput(struct inode *inode)
  __CPROVER_requires(inode != (struct inode *)0)
  __CPROVER_requires(inode_live(inode) == 1)
  __CPROVER_assigns();
