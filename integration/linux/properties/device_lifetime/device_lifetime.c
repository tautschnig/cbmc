/// \file
/// device_lifetime.c — reference implementation of the
/// device_lifetime property module.  Mirrors the cred/refcount/
/// kobject pattern: per-pointer ghost usage count.

#include "device_lifetime.h"

#ifndef DEVICE_LIFETIME_GHOST_TABLE_SIZE
#  define DEVICE_LIFETIME_GHOST_TABLE_SIZE 16
#endif

struct device_ghost_entry
{
  struct device *key;
  unsigned int usage;
};

static struct device_ghost_entry
  device_ghost_table[DEVICE_LIFETIME_GHOST_TABLE_SIZE];
static unsigned int device_ghost_table_len = 0;

static struct device_ghost_entry *
device_ghost_find(struct device *dev)
{
  for(unsigned int i = 0; i < device_ghost_table_len; i++)
  {
    if(device_ghost_table[i].key == dev)
      return &device_ghost_table[i];
  }
  return (struct device_ghost_entry *)0;
}

static struct device_ghost_entry *
device_ghost_find_or_add(struct device *dev)
{
  struct device_ghost_entry *existing =
    device_ghost_find(dev);
  if(existing)
    return existing;
  if(device_ghost_table_len >= DEVICE_LIFETIME_GHOST_TABLE_SIZE)
    return (struct device_ghost_entry *)0;
  struct device_ghost_entry *slot =
    &device_ghost_table[device_ghost_table_len++];
  slot->key = dev;
  slot->usage = 0;
  return slot;
}

void device_lifetime_init(struct device *dev,
                              unsigned int usage)
{
  struct device_ghost_entry *slot =
    device_ghost_find_or_add(dev);
  if(slot)
    slot->usage = usage;
}

void device_lifetime_get(struct device *dev)
{
  struct device_ghost_entry *slot =
    device_ghost_find_or_add(dev);
  if(slot)
    slot->usage++;
}

void device_lifetime_put(struct device *dev)
{
  struct device_ghost_entry *slot =
    device_ghost_find(dev);
  if(!slot)
    return;
  if(slot->usage > 0)
    slot->usage--;
}

unsigned int device_lifetime_usage(struct device *dev)
{
  struct device_ghost_entry *slot =
    device_ghost_find(dev);
  if(!slot)
    return 0;
  return slot->usage;
}

int device_live(struct device *dev)
{
  if(!dev)
    return 0;
  return device_lifetime_usage(dev) > 0 ? 1 : 0;
}
