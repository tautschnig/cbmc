/// \file
/// pipe_buffer.c — reference implementation of the pipe_buffer
/// property module.
///
/// Ghost state: a small pointer-keyed side table tracking which
/// `struct pipe_buffer *` has been "populated" by the current
/// writer (and therefore may legitimately have `CAN_MERGE` set).
/// Following the page_provenance module pattern, table size is
/// fixed and configurable via the `PIPE_BUF_GHOST_TABLE_SIZE`
/// macro.

#include "pipe_buffer.h"

#ifndef PIPE_BUF_GHOST_TABLE_SIZE
#  define PIPE_BUF_GHOST_TABLE_SIZE 16
#endif

struct pipe_buf_ghost_entry
{
  struct pipe_buffer *key;
  int populated;
};

static struct pipe_buf_ghost_entry pipe_buf_ghost_table[PIPE_BUF_GHOST_TABLE_SIZE];
static unsigned int pipe_buf_ghost_table_len = 0;

static struct pipe_buf_ghost_entry *pipe_buf_ghost_find(
  struct pipe_buffer *buf)
{
  for(unsigned int i = 0; i < pipe_buf_ghost_table_len; i++)
  {
    if(pipe_buf_ghost_table[i].key == buf)
      return &pipe_buf_ghost_table[i];
  }
  return (struct pipe_buf_ghost_entry *)0;
}

static struct pipe_buf_ghost_entry *pipe_buf_ghost_find_or_add(
  struct pipe_buffer *buf)
{
  struct pipe_buf_ghost_entry *existing = pipe_buf_ghost_find(buf);
  if(existing)
    return existing;
  if(pipe_buf_ghost_table_len >= PIPE_BUF_GHOST_TABLE_SIZE)
    return (struct pipe_buf_ghost_entry *)0;
  struct pipe_buf_ghost_entry *slot =
    &pipe_buf_ghost_table[pipe_buf_ghost_table_len++];
  slot->key = buf;
  slot->populated = 0;
  return slot;
}

void pipe_buffer_mark_populated(struct pipe_buffer *buf)
{
  struct pipe_buf_ghost_entry *slot = pipe_buf_ghost_find_or_add(buf);
  if(slot)
    slot->populated = 1;
}

void pipe_buffer_mark_taken_over(struct pipe_buffer *buf)
{
  struct pipe_buf_ghost_entry *slot = pipe_buf_ghost_find_or_add(buf);
  if(slot)
    slot->populated = 0;
}

int pipe_buffer_is_populated(struct pipe_buffer *buf)
{
  struct pipe_buf_ghost_entry *slot = pipe_buf_ghost_find(buf);
  if(!slot)
    return 0;
  return slot->populated;
}

int pipe_buf_merge_safe(struct pipe_buffer *buf)
{
  if(!buf)
    return 1;
  // If CAN_MERGE is set, the buffer must have been populated by
  // the current writer (ghost says populated).  Taking-over a
  // buffer without clearing CAN_MERGE is exactly the Dirty Pipe
  // bug pattern.
  int has_merge = (buf->flags & PIPE_BUF_FLAG_CAN_MERGE) != 0;
  if(has_merge && !pipe_buffer_is_populated(buf))
    return 0;
  return 1;
}
