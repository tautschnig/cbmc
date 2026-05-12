/// \file
/// pipe_buffer_kernel_adapter.c — attaches the pipe_buffer
/// property module's `pipe_buf_merge_safe` predicate as a contract
/// precondition on the kernel's `pipe_buf_release` API.
///
/// ## Why pipe_buf_release?
///
/// `pipe_buf_release` is a `static inline` in
/// `<linux/pipe_fs_i.h>` called from every pipe/splice code path
/// that hands a `struct pipe_buffer` off to its ops->release
/// callback.  Under `goto-cc --export-file-local-symbols` it gets
/// a mangled name `__CPROVER_file_local_pipe_fs_i_h_pipe_buf_release`
/// in every kernel TU that references it.  Attaching the contract
/// to both the unmangled and the mangled forms gives the scan a
/// target that is both meaningful (every pipe_buffer consumer
/// touches it) and reliably present in linked kernel binaries.
///
/// ## The precondition
///
///     __CPROVER_requires(pipe_buf_merge_safe(buf) == 1)
///
/// Translation: at every point the kernel hands a pipe_buffer off
/// to its release callback, the CAN_MERGE flag must be consistent
/// with the ghost "populated by current writer" state.  Dirty
/// Pipe (CVE-2022-0847) was a violation of this invariant:
/// copy_page_to_iter_pipe took over a buffer without resetting
/// flags, leaving CAN_MERGE set on a slot the next writer had
/// not populated.  A subsequent pipe_buf_release on that slot
/// would see an inconsistent state.
///
/// ## Struct layout
///
/// We re-declare the kernel's `struct pipe_buffer` here with the
/// exact field ordering from Linux 5.10's
/// `<linux/pipe_fs_i.h>` so the goto-cc linker unifies our
/// `buf->flags` and `buf->page` accesses with the kernel TU's.
/// The property module's abstract struct (in
/// `properties/pipe_buffer/pipe_buffer.h`) uses a two-field
/// reduction; we cannot import it here because the kernel's
/// struct has more fields.

// ---------------------------------------------------------------------------
// Kernel type declarations.
// ---------------------------------------------------------------------------

struct page;
struct pipe_inode_info;
struct pipe_buf_operations;

// Matches include/linux/pipe_fs_i.h on Linux 5.10 (and unchanged
// through at least 6.x for these fields).
struct pipe_buffer
{
  struct page *page;
  unsigned int offset, len;
  const struct pipe_buf_operations *ops;
  unsigned int flags;
  unsigned long private;
};

// Matching the property module's constant, which in turn matches
// the kernel's PIPE_BUF_FLAG_CAN_MERGE bit.
#define PIPE_BUF_FLAG_CAN_MERGE 0x10

// ---------------------------------------------------------------------------
// Ghost state API, provided by
// properties/pipe_buffer/pipe_buffer.c.  The adapter declares
// the subset it needs as extern; the linker resolves them.
// ---------------------------------------------------------------------------

int pipe_buffer_is_populated(struct pipe_buffer *buf);
void pipe_buffer_mark_populated(struct pipe_buffer *buf);
void pipe_buffer_mark_taken_over(struct pipe_buffer *buf);

// ---------------------------------------------------------------------------
// Predicate.  Pure and safe to call inside a __CPROVER_requires.
// ---------------------------------------------------------------------------

int pipe_buf_merge_safe(struct pipe_buffer *buf)
{
  if(!buf)
    return 1;
  int has_merge = (buf->flags & PIPE_BUF_FLAG_CAN_MERGE) != 0;
  if(has_merge && !pipe_buffer_is_populated(buf))
    return 0;
  return 1;
}

// ---------------------------------------------------------------------------
// Contracts.
// ---------------------------------------------------------------------------

// Contract on the static-inline form the kernel uses internally.
// `goto-cc --export-file-local-symbols` exposes it under this
// mangled name in every kernel TU that `#include`s pipe_fs_i.h.
void __CPROVER_file_local_pipe_fs_i_h_pipe_buf_release(
  struct pipe_inode_info *pipe,
  struct pipe_buffer *buf) __CPROVER_requires(buf != (struct pipe_buffer *)0)
  __CPROVER_requires(pipe_buf_merge_safe(buf) == 1) __CPROVER_assigns();

// External-name contract, for the direct-call harness which
// links against the unmangled symbol.
void pipe_buf_release(struct pipe_inode_info *pipe, struct pipe_buffer *buf)
  __CPROVER_requires(buf != (struct pipe_buffer *)0)
    __CPROVER_requires(pipe_buf_merge_safe(buf) == 1) __CPROVER_assigns();
