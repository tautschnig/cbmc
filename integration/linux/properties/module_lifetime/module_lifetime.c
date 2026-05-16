/// \file
/// module_lifetime.c — reference implementation of the
/// module_lifetime property module.  Mirrors the cred/refcount/
/// kobject pattern: per-pointer ghost usage count.

#include "module_lifetime.h"

#ifndef MODULE_LIFETIME_GHOST_TABLE_SIZE
#  define MODULE_LIFETIME_GHOST_TABLE_SIZE 16
#endif

struct module_ghost_entry
{
  struct module *key;
  unsigned int usage;
};

static struct module_ghost_entry
  module_ghost_table[MODULE_LIFETIME_GHOST_TABLE_SIZE];
static unsigned int module_ghost_table_len = 0;

static struct module_ghost_entry *
module_ghost_find(struct module *module)
{
  for(unsigned int i = 0; i < module_ghost_table_len; i++)
  {
    if(module_ghost_table[i].key == module)
      return &module_ghost_table[i];
  }
  return (struct module_ghost_entry *)0;
}

static struct module_ghost_entry *
module_ghost_find_or_add(struct module *module)
{
  struct module_ghost_entry *existing =
    module_ghost_find(module);
  if(existing)
    return existing;
  if(module_ghost_table_len >= MODULE_LIFETIME_GHOST_TABLE_SIZE)
    return (struct module_ghost_entry *)0;
  struct module_ghost_entry *slot =
    &module_ghost_table[module_ghost_table_len++];
  slot->key = module;
  slot->usage = 0;
  return slot;
}

void module_lifetime_init(struct module *module,
                              unsigned int usage)
{
  struct module_ghost_entry *slot =
    module_ghost_find_or_add(module);
  if(slot)
    slot->usage = usage;
}

void module_lifetime_get(struct module *module)
{
  struct module_ghost_entry *slot =
    module_ghost_find_or_add(module);
  if(slot)
    slot->usage++;
}

void module_lifetime_put(struct module *module)
{
  struct module_ghost_entry *slot =
    module_ghost_find(module);
  if(!slot)
    return;
  if(slot->usage > 0)
    slot->usage--;
}

unsigned int module_lifetime_usage(struct module *module)
{
  struct module_ghost_entry *slot =
    module_ghost_find(module);
  if(!slot)
    return 0;
  return slot->usage;
}

int module_live(struct module *module)
{
  if(!module)
    return 0;
  return module_lifetime_usage(module) > 0 ? 1 : 0;
}
