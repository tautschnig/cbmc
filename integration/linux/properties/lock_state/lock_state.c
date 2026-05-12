/// \file
/// lock_state.c — reference implementation of the lock_state
/// property module.  Follows the cred_lifetime / pipe_buffer
/// ghost-table-keyed-by-pointer template.

#include "lock_state.h"

#ifndef LOCK_STATE_GHOST_TABLE_SIZE
#  define LOCK_STATE_GHOST_TABLE_SIZE 16
#endif

struct lock_state_ghost_entry
{
  struct mutex *key;
  unsigned int held_count;
};

static struct lock_state_ghost_entry
  lock_state_ghost_table[LOCK_STATE_GHOST_TABLE_SIZE];
static unsigned int lock_state_ghost_table_len = 0;

static struct lock_state_ghost_entry *lock_state_ghost_find(struct mutex *m)
{
  for(unsigned int i = 0; i < lock_state_ghost_table_len; i++)
  {
    if(lock_state_ghost_table[i].key == m)
      return &lock_state_ghost_table[i];
  }
  return (struct lock_state_ghost_entry *)0;
}

static struct lock_state_ghost_entry *
lock_state_ghost_find_or_add(struct mutex *m)
{
  struct lock_state_ghost_entry *existing = lock_state_ghost_find(m);
  if(existing)
    return existing;
  if(lock_state_ghost_table_len >= LOCK_STATE_GHOST_TABLE_SIZE)
    return (struct lock_state_ghost_entry *)0;
  struct lock_state_ghost_entry *slot =
    &lock_state_ghost_table[lock_state_ghost_table_len++];
  slot->key = m;
  slot->held_count = 0;
  return slot;
}

void lock_state_lock(struct mutex *m)
{
  struct lock_state_ghost_entry *slot = lock_state_ghost_find_or_add(m);
  if(slot)
    slot->held_count++;
}

void lock_state_unlock_ghost(struct mutex *m)
{
  struct lock_state_ghost_entry *slot = lock_state_ghost_find(m);
  if(slot && slot->held_count > 0)
    slot->held_count--;
}

void lock_state_reset(struct mutex *m)
{
  struct lock_state_ghost_entry *slot = lock_state_ghost_find(m);
  if(slot)
    slot->held_count = 0;
}

unsigned int lock_state_held_count(struct mutex *m)
{
  struct lock_state_ghost_entry *slot = lock_state_ghost_find(m);
  if(!slot)
    return 0;
  return slot->held_count;
}

int lock_held(struct mutex *m)
{
  if(!m)
    return 0;
  return lock_state_held_count(m) > 0 ? 1 : 0;
}
