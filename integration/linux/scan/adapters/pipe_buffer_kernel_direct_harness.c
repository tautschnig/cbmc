/// \file
/// pipe_buffer_kernel_direct_harness.c — direct-call harness for
/// the pipe_buffer property module / Dirty Pipe (CVE-2022-0847).
///
/// Mirrors the design of aead_kernel_direct_harness.c: build a
/// kernel-layout `struct pipe_buffer` explicitly (vulnerable or
/// safe, selected by `-DFIXED`) and call the contract target
/// (`pipe_buf_release`).
///
/// ## Dirty Pipe shape
///
/// The bug was `copy_page_to_iter_pipe` taking over a pipe_buffer
/// slot for a new page without resetting `buf->flags`, leaving
/// `PIPE_BUF_FLAG_CAN_MERGE` from the previous occupant.  A
/// subsequent writer that merged into the slot could overwrite
/// pages it had no right to touch.
///
/// ### Vulnerable harness
///
///   1. Writer A populates the slot with CAN_MERGE set (legit).
///   2. The slot is taken over for a new page but flags are NOT
///      reset (the bug).
///   3. `pipe_buf_release` is called on the slot — its contract
///      precondition `pipe_buf_merge_safe(buf) == 1` must fire
///      because CAN_MERGE is still set but the buffer has been
///      taken over (ghost populated = 0).
///
/// ### Safe harness (-DFIXED)
///
/// Step 2 resets `buf->flags = 0` before the take-over; the
/// precondition passes.
///
/// ## Usage
///
/// Compiled twice by scan.py: once without `-DFIXED` (vulnerable
/// shape, expect FAILED on the contract precondition) and once
/// with (safe shape, expect SUCCESSFUL).  Both runs link against
/// the same kernel binary, adapter, stubs, and property module.

// Compiled under the kernel's `-nostdinc` regime.
typedef unsigned long size_t;

struct page
{
  unsigned long _pad;
};
struct pipe_inode_info;
struct pipe_buf_operations;

// Match include/linux/pipe_fs_i.h on Linux 5.10.
struct pipe_buffer
{
  struct page *page;
  unsigned int offset, len;
  const struct pipe_buf_operations *ops;
  unsigned int flags;
  unsigned long private;
};

#define PIPE_BUF_FLAG_CAN_MERGE 0x10

// Ghost-state API from the property module.
void pipe_buffer_mark_populated(struct pipe_buffer *buf);
void pipe_buffer_mark_taken_over(struct pipe_buffer *buf);

// Contract target (declared by the adapter; body supplied by the
// linked kernel binary or left as a bodyless extern for
// --replace-call-with-contract to rewrite at call sites).
void pipe_buf_release(struct pipe_inode_info *pipe, struct pipe_buffer *buf);

int main(void)
{
  struct pipe_buffer buf;
  struct page page_a, page_b;

  // Step 1: writer A populates the buffer with CAN_MERGE set
  // (semantically OK — A put the data there and may merge more).
  buf.page = &page_a;
  buf.offset = 0;
  buf.len = 4096;
  buf.ops = (const struct pipe_buf_operations *)0;
  buf.flags = PIPE_BUF_FLAG_CAN_MERGE;
  buf.private = 0;
  pipe_buffer_mark_populated(&buf);

  // Step 2: take over the slot for a new page.
#ifndef FIXED
  // ---------- Vulnerable shape ----------
  // Dirty Pipe: flags NOT reset.
  buf.page = &page_b;
#else
  // ---------- Fixed shape ----------
  // Reset flags to 0 before handing off the slot to the new
  // occupant (the upstream fix for copy_page_to_iter_pipe).
  buf.flags = 0;
  buf.page = &page_b;
#endif
  pipe_buffer_mark_taken_over(&buf);

  // Step 3: a subsequent pipe code path releases the slot.
  // The contract on pipe_buf_release requires
  // `pipe_buf_merge_safe(buf) == 1`; it must FIRE in the
  // vulnerable shape (CAN_MERGE carried over an unreset
  // take-over) and PASS in the fixed shape.
  struct pipe_inode_info *pipe = (struct pipe_inode_info *)0;
  pipe_buf_release(pipe, &buf);

  return 0;
}
