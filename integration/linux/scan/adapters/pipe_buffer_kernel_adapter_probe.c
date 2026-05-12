/// \file
/// pipe_buffer_kernel_adapter_probe.c — vacuity-probe variant of
/// pipe_buffer_kernel_adapter.c.  Replaces the substantive
/// precondition with `__CPROVER_requires(0 == 1)`; scan.py links
/// this in place of the real adapter for a one-shot probe run
/// that MUST fail, proving the contract call site is reachable.

struct page;
struct pipe_inode_info;
struct pipe_buf_operations;

struct pipe_buffer
{
  struct page *page;
  unsigned int offset, len;
  const struct pipe_buf_operations *ops;
  unsigned int flags;
  unsigned long private;
};

// Contract on the mangled static-inline form.
void __CPROVER_file_local_pipe_fs_i_h_pipe_buf_release(
  struct pipe_inode_info *pipe,
  struct pipe_buffer *buf) __CPROVER_requires(0 == 1) __CPROVER_assigns();

// External-name contract.
void pipe_buf_release(struct pipe_inode_info *pipe, struct pipe_buffer *buf)
  __CPROVER_requires(0 == 1) __CPROVER_assigns();
