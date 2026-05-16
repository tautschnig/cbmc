/// \file
/// inode_kernel_direct_harness.c — direct-call
/// harness for the inode_lifetime property module.
///
/// Mirrors the cred / kobject pattern: build a sentinel, register
/// it with the ghost table at usage=1, call `iput`
/// (contract holds), drop the ghost, call again (contract fires).
/// `-DFIXED` initialises with usage=2 so both puts land on a
/// still-live object.

typedef unsigned long size_t;

struct inode;

void inode_lifetime_init(struct inode *inode,
                              unsigned int usage);
void inode_lifetime_get(struct inode *inode);
void inode_lifetime_put(struct inode *inode);

void iput(struct inode *inode);

int main(void)
{
  static char inode_sentinel[1024];
  struct inode *inode =
    (struct inode *)inode_sentinel;

#ifndef FIXED
  inode_lifetime_init(inode, 1);
#else
  inode_lifetime_init(inode, 2);
#endif

  iput(inode);
  inode_lifetime_put(inode);

  iput(inode);
  inode_lifetime_put(inode);

  return 0;
}
