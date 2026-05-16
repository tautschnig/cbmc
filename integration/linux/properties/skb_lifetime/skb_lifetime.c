/// \file
/// skb_lifetime.c — reference implementation of the
/// skb_lifetime property module.  Mirrors the cred/refcount/
/// kobject pattern: per-pointer ghost usage count.

#include "skb_lifetime.h"

#ifndef SKB_LIFETIME_GHOST_TABLE_SIZE
#  define SKB_LIFETIME_GHOST_TABLE_SIZE 16
#endif

struct skb_ghost_entry
{
  struct sk_buff *key;
  unsigned int usage;
};

static struct skb_ghost_entry
  skb_ghost_table[SKB_LIFETIME_GHOST_TABLE_SIZE];
static unsigned int skb_ghost_table_len = 0;

static struct skb_ghost_entry *
skb_ghost_find(struct sk_buff *skb)
{
  for(unsigned int i = 0; i < skb_ghost_table_len; i++)
  {
    if(skb_ghost_table[i].key == skb)
      return &skb_ghost_table[i];
  }
  return (struct skb_ghost_entry *)0;
}

static struct skb_ghost_entry *
skb_ghost_find_or_add(struct sk_buff *skb)
{
  struct skb_ghost_entry *existing =
    skb_ghost_find(skb);
  if(existing)
    return existing;
  if(skb_ghost_table_len >= SKB_LIFETIME_GHOST_TABLE_SIZE)
    return (struct skb_ghost_entry *)0;
  struct skb_ghost_entry *slot =
    &skb_ghost_table[skb_ghost_table_len++];
  slot->key = skb;
  slot->usage = 0;
  return slot;
}

void skb_lifetime_init(struct sk_buff *skb,
                              unsigned int usage)
{
  struct skb_ghost_entry *slot =
    skb_ghost_find_or_add(skb);
  if(slot)
    slot->usage = usage;
}

void skb_lifetime_get(struct sk_buff *skb)
{
  struct skb_ghost_entry *slot =
    skb_ghost_find_or_add(skb);
  if(slot)
    slot->usage++;
}

void skb_lifetime_put(struct sk_buff *skb)
{
  struct skb_ghost_entry *slot =
    skb_ghost_find(skb);
  if(!slot)
    return;
  if(slot->usage > 0)
    slot->usage--;
}

unsigned int skb_lifetime_usage(struct sk_buff *skb)
{
  struct skb_ghost_entry *slot =
    skb_ghost_find(skb);
  if(!slot)
    return 0;
  return slot->usage;
}

int skb_live(struct sk_buff *skb)
{
  if(!skb)
    return 0;
  return skb_lifetime_usage(skb) > 0 ? 1 : 0;
}
