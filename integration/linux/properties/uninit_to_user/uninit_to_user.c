/// \file
/// uninit_to_user.c — reference impl.

#include "uninit_to_user.h"

#ifndef UNINIT_TABLE_SIZE
#  define UNINIT_TABLE_SIZE 16
#endif

struct uninit_entry
{
  const void *key;
  int initialised;
};

static struct uninit_entry table[UNINIT_TABLE_SIZE];
static unsigned int table_len = 0;

static struct uninit_entry *find(const void *p)
{
  for(unsigned int i = 0; i < table_len; i++)
    if(table[i].key == p)
      return &table[i];
  return (struct uninit_entry *)0;
}

static struct uninit_entry *find_or_add(const void *p)
{
  struct uninit_entry *e = find(p);
  if(e)
    return e;
  if(table_len >= UNINIT_TABLE_SIZE)
    return (struct uninit_entry *)0;
  struct uninit_entry *slot = &table[table_len++];
  slot->key = p;
  slot->initialised = 0;
  return slot;
}

void mark_initialised(const void *p)
{
  struct uninit_entry *e = find_or_add(p);
  if(e)
    e->initialised = 1;
}

void mark_uninitialised(const void *p)
{
  struct uninit_entry *e = find_or_add(p);
  if(e)
    e->initialised = 0;
}

int is_initialised(const void *p)
{
  if(!p)
    return 0;
  struct uninit_entry *e = find(p);
  if(!e)
    return 0;
  return e->initialised;
}
