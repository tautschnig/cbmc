/// \file
/// kobject_lifetime.c — reference implementation of the
/// kobject_lifetime property module.  Mirrors cred_lifetime:
/// per-pointer ghost usage count.

#include "kobject_lifetime.h"

#ifndef KOBJECT_LIFETIME_GHOST_TABLE_SIZE
#  define KOBJECT_LIFETIME_GHOST_TABLE_SIZE 16
#endif

struct kobject_ghost_entry
{
  struct kobject *key;
  unsigned int usage;
};

static struct kobject_ghost_entry
  kobject_ghost_table[KOBJECT_LIFETIME_GHOST_TABLE_SIZE];
static unsigned int kobject_ghost_table_len = 0;

static struct kobject_ghost_entry *kobject_ghost_find(struct kobject *k)
{
  for(unsigned int i = 0; i < kobject_ghost_table_len; i++)
  {
    if(kobject_ghost_table[i].key == k)
      return &kobject_ghost_table[i];
  }
  return (struct kobject_ghost_entry *)0;
}

static struct kobject_ghost_entry *kobject_ghost_find_or_add(struct kobject *k)
{
  struct kobject_ghost_entry *existing = kobject_ghost_find(k);
  if(existing)
    return existing;
  if(kobject_ghost_table_len >= KOBJECT_LIFETIME_GHOST_TABLE_SIZE)
    return (struct kobject_ghost_entry *)0;
  struct kobject_ghost_entry *slot =
    &kobject_ghost_table[kobject_ghost_table_len++];
  slot->key = k;
  slot->usage = 0;
  return slot;
}

void kobject_lifetime_init(struct kobject *k, unsigned int usage)
{
  struct kobject_ghost_entry *slot = kobject_ghost_find_or_add(k);
  if(slot)
    slot->usage = usage;
}

void kobject_lifetime_get(struct kobject *k)
{
  struct kobject_ghost_entry *slot = kobject_ghost_find_or_add(k);
  if(slot)
    slot->usage++;
}

void kobject_lifetime_put(struct kobject *k)
{
  struct kobject_ghost_entry *slot = kobject_ghost_find(k);
  // No entry = unknown kobject (not tracked by the harness).
  // Treat put as a no-op; the property predicate will report it
  // as not-live, which is the safe default.
  if(!slot)
    return;
  if(slot->usage > 0)
    slot->usage--;
}

unsigned int kobject_lifetime_usage(struct kobject *k)
{
  struct kobject_ghost_entry *slot = kobject_ghost_find(k);
  if(!slot)
    return 0;
  return slot->usage;
}

int kobject_live(struct kobject *k)
{
  if(!k)
    return 0;
  return kobject_lifetime_usage(k) > 0 ? 1 : 0;
}
