/// \file
/// fput_lifetime.c — reference implementation of the
/// fput_lifetime property module.  Mirrors the cred/refcount/
/// kobject pattern: per-pointer ghost usage count.

#include "fput_lifetime.h"

#ifndef FPUT_LIFETIME_GHOST_TABLE_SIZE
#  define FPUT_LIFETIME_GHOST_TABLE_SIZE 16
#endif

struct fput_ghost_entry
{
  struct file *key;
  unsigned int usage;
};

static struct fput_ghost_entry
  fput_ghost_table[FPUT_LIFETIME_GHOST_TABLE_SIZE];
static unsigned int fput_ghost_table_len = 0;

static struct fput_ghost_entry *
fput_ghost_find(struct file *file)
{
  for(unsigned int i = 0; i < fput_ghost_table_len; i++)
  {
    if(fput_ghost_table[i].key == file)
      return &fput_ghost_table[i];
  }
  return (struct fput_ghost_entry *)0;
}

static struct fput_ghost_entry *
fput_ghost_find_or_add(struct file *file)
{
  struct fput_ghost_entry *existing =
    fput_ghost_find(file);
  if(existing)
    return existing;
  if(fput_ghost_table_len >= FPUT_LIFETIME_GHOST_TABLE_SIZE)
    return (struct fput_ghost_entry *)0;
  struct fput_ghost_entry *slot =
    &fput_ghost_table[fput_ghost_table_len++];
  slot->key = file;
  slot->usage = 0;
  return slot;
}

void fput_lifetime_init(struct file *file,
                              unsigned int usage)
{
  struct fput_ghost_entry *slot =
    fput_ghost_find_or_add(file);
  if(slot)
    slot->usage = usage;
}

void fput_lifetime_get(struct file *file)
{
  struct fput_ghost_entry *slot =
    fput_ghost_find_or_add(file);
  if(slot)
    slot->usage++;
}

void fput_lifetime_put(struct file *file)
{
  struct fput_ghost_entry *slot =
    fput_ghost_find(file);
  if(!slot)
    return;
  if(slot->usage > 0)
    slot->usage--;
}

unsigned int fput_lifetime_usage(struct file *file)
{
  struct fput_ghost_entry *slot =
    fput_ghost_find(file);
  if(!slot)
    return 0;
  return slot->usage;
}

int fput_live(struct file *file)
{
  if(!file)
    return 0;
  return fput_lifetime_usage(file) > 0 ? 1 : 0;
}
