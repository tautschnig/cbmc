/// \file
/// sock_lifetime.c — reference implementation of the
/// sock_lifetime property module.  Mirrors the cred/refcount/
/// kobject pattern: per-pointer ghost usage count.

#include "sock_lifetime.h"

#ifndef SOCK_LIFETIME_GHOST_TABLE_SIZE
#  define SOCK_LIFETIME_GHOST_TABLE_SIZE 16
#endif

struct sock_ghost_entry
{
  struct sock *key;
  unsigned int usage;
};

static struct sock_ghost_entry
  sock_ghost_table[SOCK_LIFETIME_GHOST_TABLE_SIZE];
static unsigned int sock_ghost_table_len = 0;

static struct sock_ghost_entry *
sock_ghost_find(struct sock *sk)
{
  for(unsigned int i = 0; i < sock_ghost_table_len; i++)
  {
    if(sock_ghost_table[i].key == sk)
      return &sock_ghost_table[i];
  }
  return (struct sock_ghost_entry *)0;
}

static struct sock_ghost_entry *
sock_ghost_find_or_add(struct sock *sk)
{
  struct sock_ghost_entry *existing =
    sock_ghost_find(sk);
  if(existing)
    return existing;
  if(sock_ghost_table_len >= SOCK_LIFETIME_GHOST_TABLE_SIZE)
    return (struct sock_ghost_entry *)0;
  struct sock_ghost_entry *slot =
    &sock_ghost_table[sock_ghost_table_len++];
  slot->key = sk;
  slot->usage = 0;
  return slot;
}

void sock_lifetime_init(struct sock *sk,
                              unsigned int usage)
{
  struct sock_ghost_entry *slot =
    sock_ghost_find_or_add(sk);
  if(slot)
    slot->usage = usage;
}

void sock_lifetime_get(struct sock *sk)
{
  struct sock_ghost_entry *slot =
    sock_ghost_find_or_add(sk);
  if(slot)
    slot->usage++;
}

void sock_lifetime_put(struct sock *sk)
{
  struct sock_ghost_entry *slot =
    sock_ghost_find(sk);
  if(!slot)
    return;
  if(slot->usage > 0)
    slot->usage--;
}

unsigned int sock_lifetime_usage(struct sock *sk)
{
  struct sock_ghost_entry *slot =
    sock_ghost_find(sk);
  if(!slot)
    return 0;
  return slot->usage;
}

int sock_live(struct sock *sk)
{
  if(!sk)
    return 0;
  return sock_lifetime_usage(sk) > 0 ? 1 : 0;
}
