# CBMC limitations encountered during Linux kernel analysis

A running log of points where CBMC's current capability constrains
the analysis work in `integration/linux/`.  Each entry records:

- a concise statement of the limitation;
- the specific symptom we saw;
- how we worked around it (if we could), and what we deferred (if we
  couldn't);
- any upstream issue/PR reference, once filed.

The file is append-only from this point forward; mark resolved items
inline rather than removing them.

## LIM-001 — `library_check.sh` fails mid-build — **RESOLVED**

**First hit:** M3 rebuild, CBMC commit range `6b6c8d326d..166a7d4af3`.

Building any target that depended on `libansi-c.a` produced

```
[N/M] Generating library-check.stamp
FAILED: src/ansi-c/library-check.stamp ...
Tests and library functions don't match.
```

with a list of `*-01` test directory names under
`regression/cbmc-library/` flagged as "tests not matching any library
function."

**Root cause.** The working tree contained 163 orphaned directories
under `regression/cbmc-library/` that had `msvc.out` / `test.out`
artifacts but no `test.desc` — remnants from a prior test run that
had not been cleaned up.  The `library_check.sh` script lists the
on-disk directory contents and diffs them against the set of
functions defined in `src/*/library/*.c`; the orphaned output
directories showed up as phantom tests.

**Fix.** Remove and re-check-out the directory:

```sh
rm -rf regression/cbmc-library
git checkout HEAD -- regression/cbmc-library
rm -f build/src/ansi-c/library-check.stamp
cmake --build build --target cbmc
```

After this, `make` succeeds without any workaround.  The upstream
`library_check.sh` itself is working correctly; the problem was
local filesystem pollution.

## LIM-002 — `goto-instrument --enforce-contract` on a function with a loop requires loop contracts — **RESOLVED via `--dfcc`**

**First hit:** M2, `page_provenance/set_page_prov`.

Running

```
goto-instrument --enforce-contract set_page_prov <in.gb> <out.gb>
```

on a function whose body contained a `for` loop produced

```
File: src/goto-instrument/contracts/contracts.cpp:1152
Condition: is_loop_free(function_body, ns, log)
Reason: Loops remain in function 'set_page_prov', assigns clause
        checking instrumentation cannot be applied.
```

and aborted with a core dump.

**Fix.** Switch to the dynamic frame-condition checking path by
prefixing the invocation with `--dfcc <harness>`:

```
goto-instrument --dfcc main --enforce-contract set_page_prov <in.gb> <out.gb>
```

DFCC accepts loop-bodied functions without pre-existing loop
invariants, and the subsequent `cbmc` run verifies the contract
with bounded unwinding of the loops (we use `--unwind 8
--unwinding-assertions` on a `PAGE_PROV_TABLE_SIZE`-2 or -3 compile).

DFCC has a different constraint instead: *only one top-level call to
the function under check per harness*.  Harnesses that want to
enforce multiple contracts either have one `main()` per target
(selected by `-DENFORCE_<name>`; see
`properties/aead/test_enforce.c` and
`properties/scatterlist/test_enforce.c`) or run `goto-instrument`
once per target.  This is minor in practice.

Status: all three property modules
(`page_provenance`, `scatterlist`, `aead`) now have working
`goto-instrument --dfcc --enforce-contract` regressions for every
one of their public functions.

## LIM-003 — `goto-instrument` aborts rather than failing gracefully — **PARTIALLY MITIGATED**

**First hit:** concurrent with LIM-002.

When `goto-instrument --enforce-contract` (the non-DFCC path)
detected a situation it could not handle, it aborted (SIGABRT with
a core dump) and wrote a `Backtrace:` dump to stderr.  Moving to
`--dfcc` removed that particular failure mode for our use cases, but
the general ergonomic issue remains: a user-visible error with a
non-zero exit code would be easier for scripting and CI than a
SIGABRT, and core dumps are not a constructive signal for an
internal invariant violation.

**Workaround.** Scripts defend against the output file not being
produced; we kept that defensive pattern in all `run.sh` scripts
even after switching to DFCC.

**Status.** Cosmetic once DFCC is the default path for us; candidate
for a small CBMC contribution to convert the invariant-violation
into a diagnostic exit.

## LIM-004 — (corrected in M4b) goto-cc preserves kernel inline calls; contract replacement works

**First hit:** M3 stretch, `crypto/algif_aead.c` from Linux 5.10.
**Status:** **RESOLVED.**  My original diagnosis was wrong.

Earlier I had claimed that the kernel's `static inline` setters
(`aead_request_set_crypt`, `sg_chain`, ...) were inlined by GCC
before `goto-cc` saw them, leaving no call sites for
`goto-instrument --replace-call-with-contract` to abstract.  That is
false.  Inspecting the actual goto binary with
`goto-instrument --show-goto-functions` shows real `CALL
aead_request_set_crypt(...)` instructions inside `_aead_recvmsg`,
and `goto-instrument --replace-call-with-contract
aead_request_set_crypt` substitutes the contract's ASSERT at those
call sites cleanly.  See `scan/adapters/aead_kernel_adapter.c` for
the working adapter.

The kernel's `static` functions do undergo name-mangling in
goto-cc's goto binary (e.g. symbol names like
`__CPROVER_file_local_<hash>_<file>_<name>$object`), which would be
a concern if our adapter needed to call any of them by name; in
that situation we would use `goto-cc --export-file-local-symbols`
(coarse) or the `crangler` tool (preferred) to expose the symbol.
For the aead adapter we only needed the kernel's non-static
`aead_request_set_crypt` (which is `static inline` but declared
non-static from goto-cc's perspective) plus our own re-implementation
of the scatterlist walkers, so neither tool was needed.

## LIM-004a — `sg_next` not materialised in `crypto/algif_aead.c`'s goto binary

**First hit:** M4b adapter design, `crypto/algif_aead.c` from Linux 5.10.

Although `sg_page` is present in the goto binary (used elsewhere in
the file), `sg_next` — also `static inline` — is not, because the
file does not call it directly.  An adapter that wants to walk a
kernel scatterlist therefore cannot rely on `sg_next` being linkable
from the binary.

**Workaround.** The aead adapter re-implements the scatterlist walk
against the kernel's bit-packed `page_link` layout directly
(matching `include/linux/scatterlist.h`).  The implementation is
small (~20 lines) and does not introduce a linkage dependency on any
static inline kernel helper.

## LIM-005 — Preprocessing depends on the host glibc laying out a header that recent glibc removed

**First hit:** M3 stretch, preparing Linux 5.10 for
`make crypto/algif_aead.o`.

Unchanged.  Environmental; not a CBMC issue.

## LIM-006 — Whole-function CBMC on a real kernel source file does not terminate without stubbing

**First hit:** M3 stretch, running `cbmc --function _aead_recvmsg` on
the compiled `crypto/algif_aead.c`.

Reconfirmed in M4b: with the aead kernel adapter linked in and
`--replace-call-with-contract aead_request_set_crypt` applied, the
goto binary's `_aead_recvmsg` carries the precondition ASSERT at the
correct call site, but `cbmc --function _aead_recvmsg --unwind 2` on
that binary does not complete within a 180-second budget
(`scan.py` reports `cbmc_status: "timeout"` for this case).

**M4c progress (partial).** Stubbing experiments on `_aead_recvmsg`:

- `goto-instrument --drop-unused-functions` prunes the call graph
  from ~2.65 MB to ~2.55 MB but does not shift the cbmc verdict from
  `timeout`.
- `goto-instrument --generate-function-body '.*' --generate-function-body-options havoc`
  fails (regex bail-out in goto-instrument's pattern parsing).  A
  narrower regex (`af_alg_.*|sock_.*|crypto_aead_.*|…`) also fails
  to produce an output file; goto-instrument silently exits without
  writing.  Possibly a latent goto-instrument bug; would merit a
  reduced reproducer and an upstream report as a follow-up.
- `cbmc --partial-loops` does not shift `_aead_recvmsg` from
  `timeout` either; the full-function exploration still blows state.

**Partial mitigation landed as a kernel-layout regression**
([`cve-2026-31431/harness_kernel.c`](../cve-2026-31431/harness_kernel.c)):
a deterministic harness that uses the kernel's bit-packed
`struct scatterlist` layout and the `aead` kernel adapter.  cbmc
verifies the vulnerable shape as `FAILED` and the fixed shape as
`SUCCESSFUL` within the regression's default budget, so the
pipeline's correctness on the real kernel struct layout is now
regression-tested end to end.  The full `_aead_recvmsg` verdict
remains `timeout` in `scan.py` output.

**Workaround direction** (future M4c follow-up): write per-helper
havocing bodies as proper C stubs rather than relying on goto-instrument's
regex-driven body generation.  Minimum viable stub set is
`af_alg_wait_for_data`, `af_alg_alloc_areq`, `af_alg_get_rsgl`,
`af_alg_count_tsgl`, `sock_kmalloc`, `crypto_aead_copy_sgl`,
`af_alg_pull_tsgl`.  Each needs to (a) return a nondet value of the
correct type and (b) set up scatterlist provenance such that the
reachable path to `aead_request_set_crypt` is exercisable.  The
`af_alg_get_rsgl` stub specifically needs to tag the rsgl pages
`PAGE_USER_WRITABLE` via `set_page_prov`, and the `af_alg_pull_tsgl`
stub needs to chain nondet-provenance pages into the destination so
both the vulnerable and safe paths are reachable.

## LIM-007 — SARIF output from CBMC — **RESOLVED**

**First hit:** M4a report-format planning.
**Status:** landed via cherry-pick of upstream
<https://github.com/diffblue/cbmc/pull/8835> into this tree.  `cbmc
--sarif-result <file>` now emits a SARIF 2.1.0 log; `scan.py --sarif
<file>` merges per-run logs into a single multi-run document.

## LIM-008 — `goto-instrument --generate-function-body` fails silently on broad regexes

**First hit:** M4c stubbing experiments on `/tmp/real.trans.gb`.

Two failure modes:

1. The wildcard regex `.*` triggers `Mismatched '(' and ')' in regular
   expression` and aborts.  goto-instrument's regex parser appears to
   have at least one ambiguity around capturing groups that `.*` as a
   whole input exposes; narrower regexes do not reliably hit it.

2. A narrower regex such as
   `af_alg_.*|sock_.*|crypto_aead_.*|kmalloc.*|kzalloc|kfree|lock_sock.*|release_sock|wait_.*|sk_.*|atomic_.*|refcount_.*|memset|get_order|array_size|arch_atomic_.*|instrument_.*|kasan_.*|kcsan_.*|skcipher_.*|__compiletime_.*|aead_sufficient_data|msg_data_left|reinit_completion|__init_.*|crypto_.*`
   parses but produces no output file and silently exits without
   an error message.  Log shows only
   `Reading GOTO program from '…'` and nothing further.

`goto-instrument` should either produce an output file or an error
exit code; silent no-op is a workflow hazard when using it from
automation.  Combined with LIM-003 (abort-on-unsupported case),
this is a consistent theme: goto-instrument's error handling in
the contracts / body-generation paths is not suitable for
non-interactive use without defensive wrapping.

**Workaround.** `scan.py` wraps every subprocess call in a timeout
and `check=True`, so a silent exit surfaces as the output file
missing at the next step.  That's enough for correctness but not
great for debuggability.

**Resolution direction.** Write per-helper havocing bodies as proper
C stubs (essentially a `scan/adapters/kernel_stubs.c` per subsystem)
rather than relying on goto-instrument's regex-driven body
generation.  Same plan under LIM-006.
