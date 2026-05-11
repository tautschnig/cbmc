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

## LIM-006 — Whole-function CBMC on a real kernel source file does not terminate without stubbing — **MITIGATED**

**First hit:** M3 stretch, running `cbmc --function _aead_recvmsg` on
the compiled `crypto/algif_aead.c`.
**Status in M4c:** cbmc *terminates* on `_aead_recvmsg` when linked
with `scan/adapters/aead_kernel_stubs.c` and
`scan/adapters/aead_kernel_harness.c`; `scan.py` now reports
`cbmc_status: "successful"` in seconds on Linux 5.10's
`crypto/algif_aead.c`.

The stubs provide havocing bodies for the AF_ALG helpers
`_aead_recvmsg` transitively touches (`af_alg_wait_for_data`,
`af_alg_alloc_areq`, `af_alg_get_rsgl`, `af_alg_count_tsgl`,
`sock_kmalloc`, `af_alg_pull_tsgl`, `crypto_aead_copy_sgl`,
`crypto_aead_{auth,req,iv}size`, `lock_sock_nested`, `release_sock`,
`msg_data_left`, `aead_sufficient_data`, plus miscellaneous small
helpers — see `aead_kernel_stubs.c` for the full set).  The harness
`__CPROVER_allocate`s 4 KiB concrete objects for the `struct socket
*sock` and `struct msghdr *msg` parameters so cbmc's pointer
analysis starts from a small set of distinct heap objects rather
than an unconstrained pointer soup.

Caveat: see the new LIM-009 for why the current SUCCESSFUL verdict
is vacuous and how to make it sound.

## LIM-009 — Kernel stubs for `_aead_recvmsg` — **PARTIALLY RESOLVED**

**First hit:** M4c, `scan.py` on
`linux_5_10/crypto/algif_aead.c`.
**Status in LIM-009 work:** the stubs now use the kernel's own
headers (compiled by `scan/compile_file.sh` with the same `-I`
flags as `crypto/algif_aead.c`) and materialise concrete
`struct page` objects:

- `af_alg_alloc_areq`: allocates `struct af_alg_async_req` sized per
  the kernel's definition and initialises the embedded
  `first_rsgl.sgl.sg` array via the kernel's `sg_init_table`.
- `af_alg_get_rsgl`: sets the first entry of
  `areq->first_rsgl.sgl.sg[]` to a concrete
  `struct page` and tags it `PAGE_USER_WRITABLE` via
  `set_page_prov`, then `sg_mark_end`s the entry.  Models user iovec
  arrival.
- `af_alg_pull_tsgl`: sets the destination SGL's first entry to a
  concrete `struct page` whose provenance is freshly nondet on each
  call (either `PAGE_USER_WRITABLE` or `PAGE_CACHE_RO`), then
  `sg_mark_end`s it.  Models splice() either safe or page-cache.

The `cbmc_status` reported by `scan.py` on the real
`crypto/algif_aead.c` goes from the LIM-009-pre "vacuous SUCCESSFUL"
to "SUCCESSFUL with concrete materialised SGL contents" — strictly
stronger, because the stubs now produce real page objects that the
contract's `sgl_all_user_writable` walker actually consults.

However, soundness of the verdict (i.e. whether CBMC is in fact
exploring the Copy Fail chain path and correctly concluding the
precondition holds, or whether some reachability oversight is
masking the failure on the vulnerable branch) still needs deeper
investigation.  Three concrete items for follow-up:

1. Confirm that the `usedpages != 0` decrypt branch
   (the one that calls `sg_chain(first_rsgl.sgl.sg, …,
   areq->tsgl)` and threads tsgl pages into the destination) is
   reachable under the current stub returns.  `af_alg_get_rsgl`'s
   nondet `int` return may not produce the `usedpages > 0` signal
   the caller's internal path check needs; adjusting the stub to
   explicitly set `areq->first_rsgl.sg_num_bytes` in a range that
   forces the chain branch is the next step.

2. Confirm that the `set_page_prov` side table retains both the
   `user_page` and `tx_page` entries simultaneously under CBMC's
   symbolic execution, and that `page_prov_of` reads the correct
   tag in both branches of the nondet.

3. Run `cbmc --trace` on a FAILURE witness (once one is produced)
   to verify the reported trace walks through the sg_chain + tsgl
   path as expected, before declaring the pipeline sound on this
   CVE.

The kernel-layout regression in
`cve-2026-31431/harness_kernel.c` remains the ground-truth
regression for the precise kernel-scatterlist shape — that
harness correctly reports `FAILED` on the vulnerable shape and
`SUCCESSFUL` on the fix, so the property modules and the contract
itself are verified sound on kernel-layout inputs.  What is not yet
verified end-to-end is whether the scan of real `_aead_recvmsg`
actually exercises the vulnerable path under the current stubs.

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
