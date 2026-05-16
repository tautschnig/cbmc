/// \file
/// dentry_lifetime.c — reference implementation of the
/// dentry_lifetime property module.  Mirrors the cred/refcount/
/// kobject pattern: per-pointer ghost usage count.

#include "dentry_lifetime.h"

#ifndef DENTRY_LIFETIME_GHOST_TABLE_SIZE
#  define DENTRY_LIFETIME_GHOST_TABLE_SIZE 16
#endif

struct dentry_ghost_entry
{
  struct dentry *key;
  unsigned int usage;
};

static struct dentry_ghost_entry
  dentry_ghost_table[DENTRY_LIFETIME_GHOST_TABLE_SIZE];
static unsigned int dentry_ghost_table_len = 0;

static struct dentry_ghost_entry *
dentry_ghost_find(struct dentry *dentry)
{
  for(unsigned int i = 0; i < dentry_ghost_table_len; i++)
  {
    if(dentry_ghost_table[i].key == dentry)
      return &dentry_ghost_table[i];
  }
  return (struct dentry_ghost_entry *)0;
}

static struct dentry_ghost_entry *
dentry_ghost_find_or_add(struct dentry *dentry)
{
  struct dentry_ghost_entry *existing =
    dentry_ghost_find(dentry);
  if(existing)
    return existing;
  if(dentry_ghost_table_len >= DENTRY_LIFETIME_GHOST_TABLE_SIZE)
    return (struct dentry_ghost_entry *)0;
  struct dentry_ghost_entry *slot =
    &dentry_ghost_table[dentry_ghost_table_len++];
  slot->key = dentry;
  slot->usage = 0;
  return slot;
}

void dentry_lifetime_init(struct dentry *dentry,
                              unsigned int usage)
{
  struct dentry_ghost_entry *slot =
    dentry_ghost_find_or_add(dentry);
  if(slot)
    slot->usage = usage;
}

void dentry_lifetime_get(struct dentry *dentry)
{
  struct dentry_ghost_entry *slot =
    dentry_ghost_find_or_add(dentry);
  if(slot)
    slot->usage++;
}

void dentry_lifetime_put(struct dentry *dentry)
{
  struct dentry_ghost_entry *slot =
    dentry_ghost_find(dentry);
  if(!slot)
    return;
  if(slot->usage > 0)
    slot->usage--;
}

unsigned int dentry_lifetime_usage(struct dentry *dentry)
{
  struct dentry_ghost_entry *slot =
    dentry_ghost_find(dentry);
  if(!slot)
    return 0;
  return slot->usage;
}

int dentry_live(struct dentry *dentry)
{
  if(!dentry)
    return 0;
  return dentry_lifetime_usage(dentry) > 0 ? 1 : 0;
}
