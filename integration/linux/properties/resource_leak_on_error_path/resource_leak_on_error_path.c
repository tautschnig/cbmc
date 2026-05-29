/// \file
/// resource_leak_on_error_path.c — reference impl.

#include "resource_leak_on_error_path.h"

#ifndef LEAK_TABLE_SIZE
#  define LEAK_TABLE_SIZE 16
#endif

struct leak_entry
{
  const void *key;
  int outstanding;
};

static struct leak_entry table[LEAK_TABLE_SIZE];
static unsigned int table_len = 0;

static struct leak_entry *find(const void *p)
{
  for(unsigned int i = 0; i < table_len; i++)
    if(table[i].key == p)
      return &table[i];
  return (struct leak_entry *)0;
}

static struct leak_entry *find_or_add(const void *p)
{
  struct leak_entry *e = find(p);
  if(e)
    return e;
  if(table_len >= LEAK_TABLE_SIZE)
    return (struct leak_entry *)0;
  struct leak_entry *slot = &table[table_len++];
  slot->key = p;
  slot->outstanding = 0;
  return slot;
}

void leak_alloc_track(const void *p)
{
  if(!p)
    return;
  struct leak_entry *e = find_or_add(p);
  if(e)
    e->outstanding = 1;
}

void leak_alloc_freed(const void *p)
{
  if(!p)
    return;
  struct leak_entry *e = find(p);
  if(e)
    e->outstanding = 0;
}

int leak_outstanding(const void *p)
{
  if(!p)
    return 0;
  struct leak_entry *e = find(p);
  if(!e)
    return 0;
  return e->outstanding;
}

int leak_any_outstanding(void)
{
  for(unsigned int i = 0; i < table_len; i++)
    if(table[i].outstanding)
      return 1;
  return 0;
}
