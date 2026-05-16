/// \file
/// of_node_lifetime.c — reference implementation of the
/// of_node_lifetime property module.  Mirrors the cred/refcount/
/// kobject pattern: per-pointer ghost usage count.

#include "of_node_lifetime.h"

#ifndef OF_NODE_LIFETIME_GHOST_TABLE_SIZE
#  define OF_NODE_LIFETIME_GHOST_TABLE_SIZE 16
#endif

struct of_node_ghost_entry
{
  struct device_node *key;
  unsigned int usage;
};

static struct of_node_ghost_entry
  of_node_ghost_table[OF_NODE_LIFETIME_GHOST_TABLE_SIZE];
static unsigned int of_node_ghost_table_len = 0;

static struct of_node_ghost_entry *
of_node_ghost_find(struct device_node *node)
{
  for(unsigned int i = 0; i < of_node_ghost_table_len; i++)
  {
    if(of_node_ghost_table[i].key == node)
      return &of_node_ghost_table[i];
  }
  return (struct of_node_ghost_entry *)0;
}

static struct of_node_ghost_entry *
of_node_ghost_find_or_add(struct device_node *node)
{
  struct of_node_ghost_entry *existing =
    of_node_ghost_find(node);
  if(existing)
    return existing;
  if(of_node_ghost_table_len >= OF_NODE_LIFETIME_GHOST_TABLE_SIZE)
    return (struct of_node_ghost_entry *)0;
  struct of_node_ghost_entry *slot =
    &of_node_ghost_table[of_node_ghost_table_len++];
  slot->key = node;
  slot->usage = 0;
  return slot;
}

void of_node_lifetime_init(struct device_node *node,
                              unsigned int usage)
{
  struct of_node_ghost_entry *slot =
    of_node_ghost_find_or_add(node);
  if(slot)
    slot->usage = usage;
}

void of_node_lifetime_get(struct device_node *node)
{
  struct of_node_ghost_entry *slot =
    of_node_ghost_find_or_add(node);
  if(slot)
    slot->usage++;
}

void of_node_lifetime_put(struct device_node *node)
{
  struct of_node_ghost_entry *slot =
    of_node_ghost_find(node);
  if(!slot)
    return;
  if(slot->usage > 0)
    slot->usage--;
}

unsigned int of_node_lifetime_usage(struct device_node *node)
{
  struct of_node_ghost_entry *slot =
    of_node_ghost_find(node);
  if(!slot)
    return 0;
  return slot->usage;
}

int of_node_live(struct device_node *node)
{
  if(!node)
    return 0;
  return of_node_lifetime_usage(node) > 0 ? 1 : 0;
}
