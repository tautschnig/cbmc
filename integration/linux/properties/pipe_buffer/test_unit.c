/// \file
/// test_unit.c — unit tests for the pipe_buffer property module.
///
/// Exercises the ghost table and the merge-safety predicate under
/// cbmc.  Run via ../run.sh.

#include "pipe_buffer.h"

#ifndef __CPROVER
#  include <assert.h>
#  define __CPROVER_assert(cond, msg) assert(cond)
#endif

int main(void)
{
  struct pipe_buffer a, b, c;

  // ---- test 1: fresh buffer, flags clear → safe ----
  a.flags = 0;
  a.page = (const void *)0x1000;
  __CPROVER_assert(pipe_buf_merge_safe(&a) == 1,
                   "flags=0 should be safe");

  // ---- test 2: populated buffer with CAN_MERGE set → safe ----
  b.flags = PIPE_BUF_FLAG_CAN_MERGE;
  b.page = (const void *)0x2000;
  pipe_buffer_mark_populated(&b);
  __CPROVER_assert(pipe_buf_merge_safe(&b) == 1,
                   "populated + CAN_MERGE should be safe");

  // ---- test 3: taken-over buffer with CAN_MERGE set → UNSAFE ----
  //        this is the Dirty Pipe pattern.
  c.flags = PIPE_BUF_FLAG_CAN_MERGE;
  c.page = (const void *)0x3000;
  pipe_buffer_mark_taken_over(&c);
  __CPROVER_assert(pipe_buf_merge_safe(&c) == 0,
                   "taken-over + CAN_MERGE must be detected as unsafe");

  // ---- test 4: taken-over but flag cleared → safe (fix direction) ----
  c.flags = 0;
  __CPROVER_assert(pipe_buf_merge_safe(&c) == 1,
                   "taken-over + flags cleared should be safe");

  return 0;
}
