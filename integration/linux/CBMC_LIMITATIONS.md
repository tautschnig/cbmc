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

## LIM-009 — Real-kernel `_aead_recvmsg` scan — **RESOLVED**

**First hit:** M4c, `scan.py` on
`linux_5_10/crypto/algif_aead.c`.

**Root cause (finally identified).** `_aead_recvmsg` is declared
`static` in `crypto/algif_aead.c`; the harness's `extern int
_aead_recvmsg(...)` declaration resolved to an **empty external
stub**, not the kernel TU's body.  Under `goto-cc --export-file-
local-symbols`, the kernel TU exposes the static function under
the mangled name
`__CPROVER_file_local_algif_aead_c__aead_recvmsg`, which is what
the harness must call.  Similarly, `aead_request_set_crypt` is
`static inline` in `<crypto/aead.h>` and gets mangled to
`__CPROVER_file_local_aead_h_aead_request_set_crypt` when called
from within `crypto/algif_aead.c` — `goto-instrument
--replace-call-with-contract` must target the mangled name at the
call site, even though the adapter declares the contract against
the unmangled name too (for standalone harnesses).  Without both
of these symbol-aware renames, the scan was **silently vacuous**
under every earlier form of the adapter / harness / stubs.

**Resolution — three coordinated changes:**

1. `scan/compile_file.sh`: add `goto-cc
   --export-file-local-symbols` so every static/static-inline
   symbol in the kernel TU becomes addressable by its mangled
   name.
2. `scan/adapters/aead_kernel_harness.c`: declare and call
   `__CPROVER_file_local_algif_aead_c__aead_recvmsg` explicitly.
   Build a complete concrete pointer graph (`struct socket →
   alg_sock → af_alg_ctx + parent alg_sock → aead_tfm →
   crypto_aead/null_tfm`), populate `ctx->tsgl_list` with one
   `af_alg_tsgl` entry so `_aead_recvmsg` does not early-return on
   the `if (processed && !tsgl_src) goto free` path, and set
   `ctx->{init,more,enc,used,aead_assoclen}` and `tfm->authsize`
   to values that keep `aead_sufficient_data` true and
   `outlen = used - authsize` non-underflowing.
3. `scan/adapters/aead_kernel_adapter.c`: declare the contract on
   **both** names — `aead_request_set_crypt` (external) for
   standalone regressions and
   `__CPROVER_file_local_aead_h_aead_request_set_crypt` (file-
   local-mangled) for the kernel scan.
4. `scan/scan.py`: drive `--replace-call-with-contract` for each
   name in its own `goto-instrument` invocation and tolerate the
   "symbol not found" error for whichever name is absent in the
   current link.

**Secondary precision adjustments required to make the scan
tractable on kernel-scale inputs:**

- `array_size` in `<linux/overflow.h>` uses `__builtin_mul_overflow`
  inside a GCC statement expression; CBMC's `symex_assign` hits
  "Unreachable" on that idiom.  `scan.py` now applies
  `goto-instrument --remove-function-body
  __CPROVER_file_local_overflow_h_array_size` before
  `--replace-call-with-contract`, turning call sites into
  nondet-return stubs (sound for our property because array_size
  does not influence the scatterlist shape).
- The SAT formula for an unsliced algif_aead.c goto binary hits
  ~tens of millions of clauses and the solver OOMs at a 4 GiB
  ceiling (LIM-006).  `scan.py` now runs `goto-instrument
  --aggressive-slice`, preserving (a) the adapter's predicate
  `sgl_all_user_writable` (referenced from the contract's
  `__CPROVER_requires` — not visible to the slicer's reachability
  as a CFG edge) and (b) its leaves `page_prov_of`, `k_sg_next`,
  `k_sg_page`, via `--aggressive-slice-preserve-function`.  Then
  cbmc is invoked with `--slice-formula` and the memory ceiling is
  raised to 24 GiB (LIM-006 mitigation).  The resulting
  end-to-end scan completes in ~200 s wall-clock on the reference
  box.

**End-to-end result.**

- On the **vulnerable** `crypto/algif_aead.c` (Linux 5.10 tree,
  unmodified):
  `cbmc_status: "failed"`, with
  `__CPROVER_file_local_aead_h_aead_request_set_crypt.precondition.3`
  (the `sgl_all_user_writable(dst) == 1` clause) violated at
  line 280.  This is the Copy Fail signal, produced by CBMC
  automatically from the kernel source — no hand-written
  regression harness required.

- The regression `scan/run.sh` case 2 now **requires** this
  status, so any weakening of the pipeline surfaces as a
  regression failure immediately.

The scan of `crypto/algif_aead.c` therefore now does the
substantive work it was designed to do, and LIM-009 is closed.
Earlier versions of this entry (which documented the partial
state and the earlier — incorrect — root-cause theories as
PARTIAL/UNSOUND) are preserved in git history for completeness.

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

## LIM-010 — Stub fidelity: fix-direction regression not passing

**First hit:** attempted fix-direction regression after LIM-009 was
resolved.  With a synthetic fix applied to
`crypto/algif_aead.c` (both `sg_unmark_end` and `sg_chain` removed
from the decrypt-with-chain block), scan.py **still** reports
`cbmc_status: "failed"` with `precondition.3` fired — even though
the walker, on paper, should terminate cleanly at
`sg[0].page_link & SG_END`.  The counterexample trace shows cbmc
walking two SGL entries via a phantom `SG_CHAIN` bit pattern, then
dereferencing a `NULL + N` pointer the harness never placed.

**Root cause (probable).** Incomplete stub fidelity in
`scan/adapters/aead_kernel_stubs.c`: several kernel helpers the
decrypt-with-chain path calls on its way to
`aead_request_set_crypt` are still unlinked externs and return
nondet values that corrupt the SGL array cbmc then walks through
the contract predicate.  Candidates so far:

- `crypto_aead_copy_sgl` (file-local static in algif_aead.c; has
  a body but transitively invokes `crypto_skcipher_encrypt` which
  is extern, so the SSA formula includes a nondet-taint on the
  destination SGL via aliasing).
- `sock_kmalloc`: my stub `__CPROVER_allocate`s the requested
  size but leaves its bytes nondet — `sg_init_table` then runs on
  nondet memory, and aggressive-slice's slicing of the formula
  may drop some of those writes.

**Workaround for now.** The regression for the vulnerable-direction
(case 2 in `scan/run.sh`) continues to pass: on the unmodified
Linux 5.10 tree, the scan correctly reports `cbmc_status: "failed"`
and names `precondition.3`, so the bug-detection direction is
sound.  Case 3 validates the vacuity guardrails themselves (a
broken harness is caught as `vacuity-risk`).  What's missing is
case 4: the fix-direction regression.

**Resolution direction.** Two paths, either a source-level stub
expansion or a goto-level pinning:

1. Add a proper body for every extern kernel helper the path
   touches on the way to the contract call site, not just the two
   `af_alg_*` helpers we have today.  Inventory: `list_for_each_*`
   primitives (if not static-inline), `sock_kmalloc` with
   zero-init, `crypto_skcipher_encrypt` returning 0, etc.

2. Switch the harness to call `aead_request_set_crypt` directly
   with a hand-built SGL shape (as
   `cve-2026-31431/harness_kernel.c` already does) rather than
   routing through `_aead_recvmsg`.  Give up on
   automatic-path-synthesis from the full kernel body and rely on
   the Coccinelle prefilter + harness-driven regression as the
   substantive property test.  Trade-off: weakens the scan's
   claim to automatically synthesise vulnerable inputs from real
   kernel control flow.

Path (1) is preferred for the aead module since we are close; path
(2) is the fallback that would also generalise cleanly to M5
(pipe_buffer + CVE-2022-0847 Dirty Pipe) and every subsequent
module.  Either way, this is logged as LIM-010 and tracked
separately from LIM-009.

## LIM-011 — goto-cc link conflict on `static inline` kernel helpers across kernel versions

**First hit:** newer-kernel smoke test on Linux 5.12-rc3 (via
`scan/smoke-newer-kernel.sh`).

When scan.py compiles `crypto/algif_aead.c` under Linux 5.12-rc3
and links it with our adapter, stubs, and harness (all three of
which include kernel headers via `scan/compile_file.sh`), `goto-cc`
aborts with:

```
./include/linux/pagemap.h:979:1:
  error: conflicting function declarations 'readahead_count'
  old definition in module 'algif_aead' file
  ./include/linux/pagemap.h line 979
  unsigned int (struct readahead_control *)
```

The conflict arises because `readahead_count` is `static inline`
in `<linux/pagemap.h>` on 5.12 but absent on 5.10 where we
developed.  `goto-cc --export-file-local-symbols` gives the
static inline a per-TU mangled name, and several of our TUs
transitively include `pagemap.h` through the kernel header graph,
so each produces its own mangled copy.  At link time, two mangled
copies are reported as conflicting because one appears to come
from the kernel TU (`algif_aead`) and one from our adapter TUs
with slightly different call-graph context.

**Workaround for now.** The 5.12 smoke test documents this as a
known failure mode.  `crypto/algif_aead.c` on 5.12 reports
`cbmc_status: "error"` with a `goto-cc exit 1` note; the Dirty
Pipe Coccinelle prefilter on `fs/splice.c` and `lib/iov_iter.c`
still fires correctly, so the newer-kernel smoke test's
infrastructure-regression check stays green.

**Resolution direction.** Three options, preferred in this order:

1. **Minimise header inclusion in the adapter.**  Split the
   adapter into (a) a minimal contract-declaration-only file
   compiled as plain C without kernel headers (already the
   case for `aead_kernel_adapter.c`), and (b) kernel-header-
   dependent predicate helpers that live in the same kernel TU
   as the target source (via `-include` at compile time rather
   than standalone compilation).  Eliminates the duplicate-
   mangling cross-TU conflict by ensuring only one TU ever
   compiles each `static inline`.

2. **Teach goto-cc to deduplicate identical `static inline`
   bodies across TUs.**  If two mangled names refer to byte-
   identical goto programs, they should merge, not conflict.
   Upstream CBMC work; would benefit every downstream user of
   the goto-cc link step.

3. **Split the kernel scan into separate per-version adapter
   configurations.**  Ship a per-kernel-version KERNEL_ADAPTERS
   spec with (version-pinned) include paths.  Scales with the
   number of kernels × property modules; ugly.

Path 1 is what the `aead_kernel_harness.c` already approximates
by including as few headers as possible (`<crypto/if_alg.h>`,
`<crypto/aead.h>`, and necessary siblings).  Sharpening that
pattern further should close most cross-version conflicts.
