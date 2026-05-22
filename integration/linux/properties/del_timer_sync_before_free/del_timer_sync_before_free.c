/// \file
/// del_timer_sync_before_free.c — reference impl.

#include "del_timer_sync_before_free.h"

#ifndef DEL_TIMER_TABLE_SIZE
#  define DEL_TIMER_TABLE_SIZE 16
#endif

struct timer_armed_entry
{
  struct timer_list *key;
  int armed;
};

static struct timer_armed_entry table[DEL_TIMER_TABLE_SIZE];
static unsigned int table_len = 0;

static struct timer_armed_entry *find(struct timer_list *t)
{
  for(unsigned int i = 0; i < table_len; i++)
    if(table[i].key == t)
      return &table[i];
  return (struct timer_armed_entry *)0;
}

static struct timer_armed_entry *find_or_add(struct timer_list *t)
{
  struct timer_armed_entry *e = find(t);
  if(e)
    return e;
  if(table_len >= DEL_TIMER_TABLE_SIZE)
    return (struct timer_armed_entry *)0;
  struct timer_armed_entry *slot = &table[table_len++];
  slot->key = t;
  slot->armed = 0;
  return slot;
}

void timer_set_armed(struct timer_list *t)
{
  struct timer_armed_entry *e = find_or_add(t);
  if(e)
    e->armed = 1;
}

void timer_clear_armed(struct timer_list *t)
{
  struct timer_armed_entry *e = find(t);
  if(e)
    e->armed = 0;
}

int timer_armed(struct timer_list *t)
{
  if(!t)
    return 0;
  struct timer_armed_entry *e = find(t);
  if(!e)
    return 0;
  return e->armed;
}
