/// \file
/// refcount_lifetime.c — reference implementation of the
/// refcount_lifetime property module.  Direct template of
/// cred_lifetime.c keyed on `refcount_t *` instead of
/// `struct cred *`.

#include "refcount_lifetime.h"

#ifndef REFCOUNT_LIFETIME_GHOST_TABLE_SIZE
#  define REFCOUNT_LIFETIME_GHOST_TABLE_SIZE 16
#endif

struct refcount_ghost_entry
{
  refcount_t *key;
  unsigned int usage;
};

static struct refcount_ghost_entry
  refcount_ghost_table[REFCOUNT_LIFETIME_GHOST_TABLE_SIZE];
static unsigned int refcount_ghost_table_len = 0;

static struct refcount_ghost_entry *refcount_ghost_find(refcount_t *r)
{
  for(unsigned int i = 0; i < refcount_ghost_table_len; i++)
  {
    if(refcount_ghost_table[i].key == r)
      return &refcount_ghost_table[i];
  }
  return (struct refcount_ghost_entry *)0;
}

static struct refcount_ghost_entry *refcount_ghost_find_or_add(refcount_t *r)
{
  struct refcount_ghost_entry *existing = refcount_ghost_find(r);
  if(existing)
    return existing;
  if(refcount_ghost_table_len >= REFCOUNT_LIFETIME_GHOST_TABLE_SIZE)
    return (struct refcount_ghost_entry *)0;
  struct refcount_ghost_entry *slot =
    &refcount_ghost_table[refcount_ghost_table_len++];
  slot->key = r;
  slot->usage = 0;
  return slot;
}

void refcount_lifetime_init(refcount_t *r, unsigned int usage)
{
  struct refcount_ghost_entry *slot = refcount_ghost_find_or_add(r);
  if(slot)
    slot->usage = usage;
}

void refcount_lifetime_inc(refcount_t *r)
{
  struct refcount_ghost_entry *slot = refcount_ghost_find_or_add(r);
  if(slot)
    slot->usage++;
}

int refcount_lifetime_dec_and_test(refcount_t *r)
{
  struct refcount_ghost_entry *slot = refcount_ghost_find(r);
  // Unknown refcount = not tracked.  Return 0 (not-reached-zero)
  // as the safe default; the property predicate reports it as
  // not-live, which will trigger the contract on any real
  // decrement site.
  if(!slot)
    return 0;
  if(slot->usage > 0)
  {
    slot->usage--;
    return slot->usage == 0 ? 1 : 0;
  }
  // Underflow: counter was already zero.  Return 0; the
  // contract precondition `refcount_live` will have caught
  // this at the call site in scan mode.
  return 0;
}

unsigned int refcount_lifetime_usage(refcount_t *r)
{
  struct refcount_ghost_entry *slot = refcount_ghost_find(r);
  if(!slot)
    return 0;
  return slot->usage;
}

int refcount_live(refcount_t *r)
{
  if(!r)
    return 0;
  return refcount_lifetime_usage(r) > 0 ? 1 : 0;
}
