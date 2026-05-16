/// \file
/// inode_lifetime.c — reference implementation of the
/// inode_lifetime property module.  Mirrors the cred/refcount/
/// kobject pattern: per-pointer ghost usage count.

#include "inode_lifetime.h"

#ifndef INODE_LIFETIME_GHOST_TABLE_SIZE
#  define INODE_LIFETIME_GHOST_TABLE_SIZE 16
#endif

struct inode_ghost_entry
{
  struct inode *key;
  unsigned int usage;
};

static struct inode_ghost_entry
  inode_ghost_table[INODE_LIFETIME_GHOST_TABLE_SIZE];
static unsigned int inode_ghost_table_len = 0;

static struct inode_ghost_entry *
inode_ghost_find(struct inode *inode)
{
  for(unsigned int i = 0; i < inode_ghost_table_len; i++)
  {
    if(inode_ghost_table[i].key == inode)
      return &inode_ghost_table[i];
  }
  return (struct inode_ghost_entry *)0;
}

static struct inode_ghost_entry *
inode_ghost_find_or_add(struct inode *inode)
{
  struct inode_ghost_entry *existing =
    inode_ghost_find(inode);
  if(existing)
    return existing;
  if(inode_ghost_table_len >= INODE_LIFETIME_GHOST_TABLE_SIZE)
    return (struct inode_ghost_entry *)0;
  struct inode_ghost_entry *slot =
    &inode_ghost_table[inode_ghost_table_len++];
  slot->key = inode;
  slot->usage = 0;
  return slot;
}

void inode_lifetime_init(struct inode *inode,
                              unsigned int usage)
{
  struct inode_ghost_entry *slot =
    inode_ghost_find_or_add(inode);
  if(slot)
    slot->usage = usage;
}

void inode_lifetime_get(struct inode *inode)
{
  struct inode_ghost_entry *slot =
    inode_ghost_find_or_add(inode);
  if(slot)
    slot->usage++;
}

void inode_lifetime_put(struct inode *inode)
{
  struct inode_ghost_entry *slot =
    inode_ghost_find(inode);
  if(!slot)
    return;
  if(slot->usage > 0)
    slot->usage--;
}

unsigned int inode_lifetime_usage(struct inode *inode)
{
  struct inode_ghost_entry *slot =
    inode_ghost_find(inode);
  if(!slot)
    return 0;
  return slot->usage;
}

int inode_live(struct inode *inode)
{
  if(!inode)
    return 0;
  return inode_lifetime_usage(inode) > 0 ? 1 : 0;
}
