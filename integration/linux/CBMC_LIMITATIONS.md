# CBMC limitations encountered during Linux kernel analysis

A running list of points where CBMC's current capability constrains
the analysis work in `integration/linux/`.  Each entry records:

- a concise statement of the limitation;
- the specific symptom we saw;
- how we worked around it (if we could), and what we deferred (if we
  couldn't);
- any upstream issue/PR reference, once filed.

The file is append-only from this point forward; mark resolved items
inline rather than removing them.

## LIM-001 — `library_check.sh` fails mid-build

**First hit:** M3 rebuild, CBMC commit range `6b6c8d326d..166a7d4af3`.

Building any target that depends on `libansi-c.a` produces

```
[N/M] Generating library-check.stamp
FAILED: src/ansi-c/library-check.stamp ...
Tests and library functions don't match.
```

The diff listed many `*-01` test directory names
(`toupper-01`, `time-01`, `vasprintf-01`, …) under `regression/cbmc-library/`
as "tests not matching any library function."  These names appear in
the working copy of the tree but not in the library function list
that the script computes from `src/*/library/*.c`.  Cause seems to
be a drift between a recent batch of `-01` tests added to
`regression/cbmc-library/` and the library-function catalog, not
anything intrinsic to the tooling.

**Workaround.** Temporarily patch `src/ansi-c/library_check.sh` to
suffix the final `diff -u` with `; true`, rebuild, revert the patch.
No local source change is committed.  See the `M3 rebuild` note in
git history for the exact steps.

**Status.** Works around cleanly.  A more robust fix upstream would
make the check advisory rather than fatal when the mismatch is
`-N` (missing function) only, or factor the catalog generation out of
the regression checker.

## LIM-002 — `goto-instrument --enforce-contract` on a function with a loop requires loop contracts

**First hit:** M2, `page_provenance/set_page_prov`.

Running

```
goto-instrument --enforce-contract set_page_prov <in.gb> <out.gb>
```

on a function whose body contains a `for` loop produces

```
File: src/goto-instrument/contracts/contracts.cpp:1152
Condition: is_loop_free(function_body, ns, log)
Reason: Loops remain in function 'set_page_prov', assigns clause
        checking instrumentation cannot be applied.
```

and aborts with a core dump rather than a diagnostic exit.

Loop contracts (`__CPROVER_loop_invariant`, `__CPROVER_loop_assigns`,
`goto-instrument --apply-loop-contracts`) exist, but the cprover
regression tree carries `quicksort_contracts_01` as `KNOWNBUG` with
the note "Loop invariants are overzealous in deciding what counts as
side effects," suggesting the feature is not robust for loops that
read shared memory.

**Workaround.** Do not `--enforce-contract` functions with loops.
Retain the contracts on them for documentation and for use at call
sites via `--replace-call-with-contract`, and verify functional
invariants through plain-cbmc assertion-style unit tests (see
`page_provenance/test_unit.c`).

**Status.** Partial workaround.  Closing the gap needs either
- robust loop-contract enforcement for loops reading shared memory
  (upstream CBMC work), or
- a redesign of the backing-store so the mutator is loop-free
  (module-local).

Filed as future work in `DESIGN.md` §8 (kernel-driven CBMC C
front-end work track) and the `page_provenance/README.md` "Known
gap" section.

## LIM-003 — `goto-instrument` aborts rather than failing gracefully

**First hit:** concurrent with LIM-002.

When `goto-instrument --enforce-contract` detects a situation it
cannot handle, it aborts (SIGABRT with a core dump) and writes a
Backtrace: dump to stderr.  A user-visible error message with a
non-zero exit code would be easier for scripting and CI, and does not
indicate an internal invariant violation.

**Workaround.** Scripts must be robust against `goto-instrument` not
producing the output file; check for the file's existence and handle
the error path.  `run.sh` in `page_provenance/` catches this.

**Status.** Ergonomic issue rather than a soundness bug; candidate
for a small CBMC contribution.

## LIM-004 — Inlined `static inline` kernel functions are not directly replaceable by a contract

**First hit:** M3 stretch, `crypto/algif_aead.c` from Linux 5.10.

The kernel's `include/crypto/aead.h` defines `aead_request_set_crypt`,
`aead_request_set_ad`, `aead_request_set_tfm` and many scatterlist
helpers as `static inline`.  After `goto-cc` compiles
`crypto/algif_aead.c`, `goto-instrument --show-goto-functions` lists
those names as *functions* (good — so they are referenceable) but
they live inside the same translation unit as every call site,
because they were inlined from a header.  `goto-instrument
--replace-call-with-contract aead_request_set_crypt` still works in
principle on such a binary, but it operates on the name inside that
one goto binary; the contract source must be compiled alongside.

More importantly, the contract we want to attach
(`__CPROVER_requires(sgl_all_user_writable(dst))`) is on *our*
module's declaration of `aead_request_set_crypt`.  Linking the kernel
goto binary with our property module's goto binary via `goto-cc` raises
a symbol-collision error: two definitions of the same name.

**Intended workaround** (for milestone M4 / M3 follow-up): two-step
pipeline

  1. `goto-instrument --remove-function-body aead_request_set_crypt
     kernel.gb kernel.stripped.gb`
  2. `goto-cc kernel.stripped.gb aead.c -o linked.gb`

Then the kernel binary calls the only remaining definition — ours —
which carries the contract, and `goto-instrument
--replace-call-with-contract aead_request_set_crypt` abstracts it at
every call site in the kernel code.  The adapter remains to
reconcile `struct scatterlist` layouts (see scatterlist module
README).  Not yet wired up; marked as the first task of M4.

**Status.** Known workflow; not yet automated by `scan/` tooling.

## LIM-005 — Preprocessing depends on the host glibc laying out a header that recent glibc removed

**First hit:** M3 stretch, preparing Linux 5.10 for `make crypto/algif_aead.o`.

On Ubuntu 22.04+ hosts, the kernel 5.10 build tree's cached
dependency files in `tools/objtool/.*.cmd` reference
`/usr/include/x86_64-linux-gnu/bits/sys_errlist.h`, which newer
glibc no longer ships.  The `make prepare` stage therefore fails
before the kernel proper starts compiling.  This is a host-
environment / kernel-version interaction, not strictly a CBMC
limitation, but it affects anyone attempting to reproduce the
milestone M3 stretch build and deserves mention.

**Workaround.** Bypass `make prepare` once the generated files
(`include/generated/autoconf.h` and friends) already exist from a
prior build, and hand-invoke `goto-cc` for the single source file
of interest using the compile command recovered from the kernel's
`.*.o.cmd` cache.  See the M3 session notes in git history for the
exact line.

**Status.** Environmental; no CBMC change needed.  We use the
workaround whenever we re-run M3 on this host.

## LIM-006 — Whole-function CBMC on a real kernel source file does not terminate without stubbing

**First hit:** M3 stretch, running `cbmc --function _aead_recvmsg` on
the compiled `crypto/algif_aead.c`.

Starting CBMC at `_aead_recvmsg` with `--unwind 1
--no-unwinding-assertions --no-standard-checks` and a 60-second
wall-clock budget does not produce a result.  `_aead_recvmsg`
transitively touches many kernel helpers that are external
declarations in the single-file build (wait queues, socket lock
operations, RCU primitives, crypto transform dispatch, etc.), and
each unresolved call expands into a nondet return plus havoc; state
blows up quickly.

This is expected behaviour for CBMC without stubs, not a bug.
Documenting it so the `scan/` driver has a clear budget / stubbing
strategy when it is built.

**Workaround direction** (for milestone M4): aggressive function
abstraction via `goto-instrument --replace-call-with-contract` for
every annotated primitive, plus `goto-instrument
--drop-unused-functions` and `--remove-function-body` for kernel
helpers whose behaviour is irrelevant to the property being checked.
A per-function CBMC budget of a few minutes should be attainable
with careful stubbing.

**Status.** Expected; resolved by the M4 pipeline design.
