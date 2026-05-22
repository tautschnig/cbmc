/// \file
/// null_after_alloc.c — reference implementation.

#include "null_after_alloc.h"

#ifndef NULL_CHECK_TABLE_SIZE
#  define NULL_CHECK_TABLE_SIZE 16
#endif

struct null_check_entry
{
  const void *key;
  int checked;
};

static struct null_check_entry table[NULL_CHECK_TABLE_SIZE];
static unsigned int table_len = 0;

static struct null_check_entry *find(const void *p)
{
  for(unsigned int i = 0; i < table_len; i++)
    if(table[i].key == p)
      return &table[i];
  return (struct null_check_entry *)0;
}

static struct null_check_entry *find_or_add(const void *p)
{
  struct null_check_entry *e = find(p);
  if(e)
    return e;
  if(table_len >= NULL_CHECK_TABLE_SIZE)
    return (struct null_check_entry *)0;
  struct null_check_entry *slot = &table[table_len++];
  slot->key = p;
  slot->checked = 0;
  return slot;
}

void assert_null_check_done(const void *p)
{
  struct null_check_entry *e = find_or_add(p);
  if(e)
    e->checked = 1;
}

void assert_null_check_clear(const void *p)
{
  struct null_check_entry *e = find(p);
  if(e)
    e->checked = 0;
}

int null_check_done(const void *p)
{
  if(!p)
    return 0;
  struct null_check_entry *e = find(p);
  if(!e)
    return 0;
  return e->checked;
}
