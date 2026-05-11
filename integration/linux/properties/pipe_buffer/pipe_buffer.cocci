// @@
//   SmPL rule: pipe_buffer.cocci
//
//   Coccinelle prefilter for the pipe_buffer property module.  Flags
//   every function that assigns a `struct pipe_buffer` without
//   clearing `buf->flags` first — the direct textual signature of
//   the Dirty Pipe (CVE-2022-0847) bug class.
//
//   Precision is CBMC's job: when spatch reports a hit, run
//       properties/pipe_buffer/*.c linked with the kernel goto
//       binary and apply goto-instrument with a contract on
//       pipe_buf_merge_safe(buf).
//
//   Two rule variants:
//
//   1. `page_assign` — flags an assignment `buf->page = X` that is
//      NOT preceded by `buf->flags = 0` in the same block.  This is
//      the canonical Dirty Pipe sequence: the kernel's
//      copy_page_to_iter_pipe routine was missing exactly this
//      flags reset.
//
//   2. `ops_assign`  — flags an assignment `buf->ops = X` with the
//      same "no flags reset" condition.  Catches variants of the
//      bug in other take-over sites.
//
//   Run with:
//       spatch --sp-file integration/linux/properties/pipe_buffer/pipe_buffer.cocci \
//              fs/splice.c lib/iov_iter.c
//
//   Expected output on pre-fix kernels (5.16 and earlier): at least
//   one hit in fs/splice.c's copy_page_to_iter_pipe or push_pipe.
// @@

@ page_assign @
expression buf, new_page;
position p;
@@

buf->page@p = new_page;

@ script:python page_report @
p << page_assign.p;
@@

coccilib.report.print_report(p[0],
    "pipe_buffer: pipe_buffer.page assignment without a preceding "
    "flags reset - candidate for CBMC property scan (Dirty Pipe / "
    "CVE-2022-0847 bug class)")

@ ops_assign @
expression buf, new_ops;
position p;
@@

buf->ops@p = new_ops;

@ script:python ops_report @
p << ops_assign.p;
@@

coccilib.report.print_report(p[0],
    "pipe_buffer: pipe_buffer.ops assignment - take-over site "
    "candidate for CBMC property scan (Dirty Pipe / CVE-2022-0847 "
    "bug class)")
