/// \file
/// pipe_buffer.h — property module for the Dirty Pipe bug class
/// (CVE-2022-0847).
///
/// ## Bug class
///
/// The kernel's pipe_buffer has a `flags` field with bits that
/// control merge behaviour across consecutive buffers on a pipe
/// ring.  In the Dirty Pipe vulnerability, a newly taken-over
/// pipe_buffer slot inherited `PIPE_BUF_FLAG_CAN_MERGE` from the
/// previous occupant, letting a writer merge new data into an
/// existing page — including pages the writer shouldn't be able to
/// touch, such as read-only page-cache pages mapped into the pipe
/// by splice().
///
/// The upstream fix was to zero `buf->flags` whenever a
/// pipe_buffer slot is newly occupied, rather than relying on the
/// previous occupant to have cleared the merge bit.
///
/// ## Abstraction
///
/// We abstract the kernel's `struct pipe_buffer` into a
/// minimal-field struct carrying only what the property needs:
/// `flags`, an opaque `page` identifier, and a `merge_ok` ghost
/// bit that tracks whether the current occupant is semantically
/// allowed to accept merges.  The kernel-adapter layer is
/// responsible for translating the real `struct pipe_buffer` into
/// this abstract form.
///
/// ## Property
///
/// The property every `pipe_buf_operations` implementation must
/// preserve, and every merge-performing helper must check, is:
///
///     pipe_buf_merge_safe(buf):
///       buf->flags has CAN_MERGE set  ==>  buf was populated by
///                                          the current writer
///                                          (i.e. ghost merge_ok
///                                           is true)
///
/// In other words, CAN_MERGE must never be set on a freshly taken-
/// over buffer whose content the current writer did not put there.
/// Dirty Pipe was a violation of this invariant.

#ifndef INTEGRATION_LINUX_PROPERTIES_PIPE_BUFFER_PIPE_BUFFER_H
#define INTEGRATION_LINUX_PROPERTIES_PIPE_BUFFER_PIPE_BUFFER_H

#include <stddef.h>
#include <stdint.h>

// Abstract struct.  Layout-compatible with the Linux kernel's
// `struct pipe_buffer` from <linux/pipe_fs_i.h> so the property
// module can be linked directly with kernel TUs (and the
// scan's `pipe_buffer_kernel_adapter.c`) without ODR conflicts.
// The property only reads `flags` and treats `page` as opaque.
//
// `struct page` and `struct pipe_buf_operations` are declared as
// dummy-field complete types here (rather than opaque forward
// declarations) so non-kernel callers — the unit test and the
// abstract CVE harnesses — can stack-allocate them as sentinels
// for ghost-table identity.  At link time with a real kernel
// binary, goto-cc unifies against the kernel's fuller struct
// since we only ever take addresses, never dereference.
struct page
{
  unsigned long _pad;
};
struct pipe_buf_operations;

struct pipe_buffer
{
  struct page *page;
  unsigned int offset, len;
  const struct pipe_buf_operations *ops;
  unsigned int flags;
  unsigned long private;
};

// Flag bit that controls merge behaviour, matching the kernel's
// definition in include/linux/pipe_fs_i.h.
#define PIPE_BUF_FLAG_CAN_MERGE 0x10

// Ghost state: `merge_ok` tracks whether the current writer
// populated the buffer (and may therefore merge into it).  The
// property module stores one ghost bit per `struct pipe_buffer *`,
// keyed by pointer identity.
void pipe_buffer_mark_populated(struct pipe_buffer *buf);
void pipe_buffer_mark_taken_over(struct pipe_buffer *buf);
int pipe_buffer_is_populated(struct pipe_buffer *buf);

// Property predicate: returns 1 iff the buffer's merge flag is
// consistent with its ghost population state.  This is what every
// contract on pipe_buf-touching kernel helpers should assert.
int pipe_buf_merge_safe(struct pipe_buffer *buf);

#endif
