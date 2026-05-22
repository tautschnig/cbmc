/// \file
/// format_string.c — reference impl.

#include "format_string.h"

#ifndef FORMAT_STRING_TABLE_SIZE
#  define FORMAT_STRING_TABLE_SIZE 16
#endif

struct fs_entry
{
  const char *key;
  int constant;
};

static struct fs_entry table[FORMAT_STRING_TABLE_SIZE];
static unsigned int table_len = 0;

static struct fs_entry *find(const char *p)
{
  for(unsigned int i = 0; i < table_len; i++)
    if(table[i].key == p)
      return &table[i];
  return (struct fs_entry *)0;
}

static struct fs_entry *find_or_add(const char *p)
{
  struct fs_entry *e = find(p);
  if(e)
    return e;
  if(table_len >= FORMAT_STRING_TABLE_SIZE)
    return (struct fs_entry *)0;
  struct fs_entry *slot = &table[table_len++];
  slot->key = p;
  slot->constant = 0;
  return slot;
}

void mark_format_constant(const char *fmt)
{
  struct fs_entry *e = find_or_add(fmt);
  if(e)
    e->constant = 1;
}

void mark_format_tainted(const char *fmt)
{
  struct fs_entry *e = find_or_add(fmt);
  if(e)
    e->constant = 0;
}

int format_is_constant(const char *fmt)
{
  if(!fmt)
    return 0;
  struct fs_entry *e = find(fmt);
  if(!e)
    return 0;
  return e->constant;
}
