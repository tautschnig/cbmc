# pipe_buffer property module

Captures the Dirty Pipe bug class (CVE-2022-0847): the kernel's
`struct pipe_buffer` has a `flags` field that gates merge
behaviour, and Dirty Pipe arose because one of the flag bits
(`PIPE_BUF_FLAG_CAN_MERGE`) was not reset when a pipe-buffer slot
was taken over by a new occupant.

## The abstract property

```
pipe_buf_merge_safe(buf)
  ==  (buf->flags & PIPE_BUF_FLAG_CAN_MERGE) is set  ==>
      buf was populated by the current writer (ghost: merge_ok)
```

i.e. `CAN_MERGE` must never be set on a freshly taken-over buffer
whose content the current writer did not put there.  The upstream
fix for CVE-2022-0847 zeroes `buf->flags` at take-over time; the
property catches the bug whether via missing reset (original) or
via any other way the invariant gets violated.

## Files

* `pipe_buffer.h`    — public header, property predicate.
* `pipe_buffer.c`    — reference implementation, ghost table.
* `test_unit.c`      — standalone CBMC unit tests (four canonical
                        cases covering vulnerable + fixed shapes).
* `pipe_buffer.cocci`— Coccinelle prefilter for the Dirty Pipe
                        textual pattern (buf->page/buf->ops
                        assignment without a preceding flags
                        reset).  Used by scan.py.
* `run.sh`           — regression driver; runs the unit tests.

## CVE reduction

See `../../cve-2022-0847/` for the vulnerable + fixed regression
pair that exercises this property module against a minimal
kernel-layout reproduction of Dirty Pipe.

## Workflow position

This is the second property module after `aead` / CVE-2026-31431.
It demonstrates the pattern generalises:

1. Abstract property module here (`properties/pipe_buffer/`).
2. CVE regression under `cve-2022-0847/` that exercises the
   property against a bug-class reproduction.
3. Coccinelle prefilter on the textual pattern.
4. (TODO) Kernel adapter + harness under
   `scan/adapters/pipe_buffer_kernel_*.c` so scan.py can apply
   the contract to real kernel source.

Step 4 is the work LIM-009 taught us to ship carefully — see
`CBMC_LIMITATIONS.md` and `scan/scan.py`'s KERNEL_ADAPTERS entry
for `aead` as the template.
