/// \file
/// use_after_free_generic.c — reference impl.

#include "use_after_free_generic.h"

#ifndef UAF_TABLE_SIZE
#  define UAF_TABLE_SIZE 16
#endif

struct uaf_entry
{
  const void *key;
  int freed;
};

static struct uaf_entry table[UAF_TABLE_SIZE];
static unsigned int table_len = 0;

static struct uaf_entry *find(const void *p)
{
  for(unsigned int i = 0; i < table_len; i++)
    if(table[i].key == p)
      return &table[i];
  return (struct uaf_entry *)0;
}

static struct uaf_entry *find_or_add(const void *p)
{
  struct uaf_entry *e = find(p);
  if(e)
    return e;
  if(table_len >= UAF_TABLE_SIZE)
    return (struct uaf_entry *)0;
  struct uaf_entry *slot = &table[table_len++];
  slot->key = p;
  slot->freed = 0;
  return slot;
}

void mark_freed(const void *p)
{
  struct uaf_entry *e = find_or_add(p);
  if(e)
    e->freed = 1;
}

void mark_alive(const void *p)
{
  struct uaf_entry *e = find_or_add(p);
  if(e)
    e->freed = 0;
}

int is_freed(const void *p)
{
  if(!p)
    return 0;
  struct uaf_entry *e = find(p);
  if(!e)
    return 0;
  return e->freed;
}
