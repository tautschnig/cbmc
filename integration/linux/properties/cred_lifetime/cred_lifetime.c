/// \file
/// cred_lifetime.c — reference implementation of the cred_lifetime
/// property module.  Follows the same ghost-table-keyed-by-pointer
/// pattern as page_provenance and pipe_buffer.

#include "cred_lifetime.h"

#ifndef CRED_LIFETIME_GHOST_TABLE_SIZE
#  define CRED_LIFETIME_GHOST_TABLE_SIZE 16
#endif

struct cred_ghost_entry
{
  struct cred *key;
  unsigned int usage;
};

static struct cred_ghost_entry cred_ghost_table[CRED_LIFETIME_GHOST_TABLE_SIZE];
static unsigned int cred_ghost_table_len = 0;

static struct cred_ghost_entry *cred_ghost_find(struct cred *c)
{
  for(unsigned int i = 0; i < cred_ghost_table_len; i++)
  {
    if(cred_ghost_table[i].key == c)
      return &cred_ghost_table[i];
  }
  return (struct cred_ghost_entry *)0;
}

static struct cred_ghost_entry *cred_ghost_find_or_add(struct cred *c)
{
  struct cred_ghost_entry *existing = cred_ghost_find(c);
  if(existing)
    return existing;
  if(cred_ghost_table_len >= CRED_LIFETIME_GHOST_TABLE_SIZE)
    return (struct cred_ghost_entry *)0;
  struct cred_ghost_entry *slot = &cred_ghost_table[cred_ghost_table_len++];
  slot->key = c;
  slot->usage = 0;
  return slot;
}

void cred_lifetime_init(struct cred *c, unsigned int usage)
{
  struct cred_ghost_entry *slot = cred_ghost_find_or_add(c);
  if(slot)
    slot->usage = usage;
}

void cred_lifetime_get(struct cred *c)
{
  struct cred_ghost_entry *slot = cred_ghost_find_or_add(c);
  if(slot)
    slot->usage++;
}

void cred_lifetime_put(struct cred *c)
{
  struct cred_ghost_entry *slot = cred_ghost_find(c);
  // No entry = unknown cred (not tracked by the harness).  Treat
  // put as a no-op; the property predicate will report it as
  // not-live, which is the safe default.
  if(!slot)
    return;
  if(slot->usage > 0)
    slot->usage--;
}

unsigned int cred_lifetime_usage(struct cred *c)
{
  struct cred_ghost_entry *slot = cred_ghost_find(c);
  if(!slot)
    return 0;
  return slot->usage;
}

int cred_live(struct cred *c)
{
  if(!c)
    return 0;
  return cred_lifetime_usage(c) > 0 ? 1 : 0;
}
