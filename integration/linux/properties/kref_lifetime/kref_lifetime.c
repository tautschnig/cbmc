/// \file
/// kref_lifetime.c — reference implementation of the
/// kref_lifetime property module.  Mirrors the cred/refcount/
/// kobject pattern: per-pointer ghost usage count.

#include "kref_lifetime.h"

#ifndef KREF_LIFETIME_GHOST_TABLE_SIZE
#  define KREF_LIFETIME_GHOST_TABLE_SIZE 16
#endif

struct kref_ghost_entry
{
  struct kref *key;
  unsigned int usage;
};

static struct kref_ghost_entry
  kref_ghost_table[KREF_LIFETIME_GHOST_TABLE_SIZE];
static unsigned int kref_ghost_table_len = 0;

static struct kref_ghost_entry *
kref_ghost_find(struct kref *kref)
{
  for(unsigned int i = 0; i < kref_ghost_table_len; i++)
  {
    if(kref_ghost_table[i].key == kref)
      return &kref_ghost_table[i];
  }
  return (struct kref_ghost_entry *)0;
}

static struct kref_ghost_entry *
kref_ghost_find_or_add(struct kref *kref)
{
  struct kref_ghost_entry *existing =
    kref_ghost_find(kref);
  if(existing)
    return existing;
  if(kref_ghost_table_len >= KREF_LIFETIME_GHOST_TABLE_SIZE)
    return (struct kref_ghost_entry *)0;
  struct kref_ghost_entry *slot =
    &kref_ghost_table[kref_ghost_table_len++];
  slot->key = kref;
  slot->usage = 0;
  return slot;
}

void kref_lifetime_init(struct kref *kref,
                              unsigned int usage)
{
  struct kref_ghost_entry *slot =
    kref_ghost_find_or_add(kref);
  if(slot)
    slot->usage = usage;
}

void kref_lifetime_get(struct kref *kref)
{
  struct kref_ghost_entry *slot =
    kref_ghost_find_or_add(kref);
  if(slot)
    slot->usage++;
}

void kref_lifetime_put(struct kref *kref)
{
  struct kref_ghost_entry *slot =
    kref_ghost_find(kref);
  if(!slot)
    return;
  if(slot->usage > 0)
    slot->usage--;
}

unsigned int kref_lifetime_usage(struct kref *kref)
{
  struct kref_ghost_entry *slot =
    kref_ghost_find(kref);
  if(!slot)
    return 0;
  return slot->usage;
}

int kref_live(struct kref *kref)
{
  if(!kref)
    return 0;
  return kref_lifetime_usage(kref) > 0 ? 1 : 0;
}
