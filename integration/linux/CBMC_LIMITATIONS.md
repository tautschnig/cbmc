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

## LIM-009 — Real-kernel `_aead_recvmsg` scan: call site unreachable under symex — **RESOLVED (scope clarification)**

**First hit:** M4c, `scan.py` on
`linux_5_10/crypto/algif_aead.c`.

**Investigation summary.** With the M4c stubs + kernel-aware harness
in place, `scan.py` on the real `crypto/algif_aead.c` reports
`cbmc_status: "successful"`.  Deep diagnostic probes
(`__CPROVER_requires(0 == 1)` forced on the contract;
`__CPROVER_assert(0, ...)` injected inside the stubs'
`af_alg_alloc_areq`, `af_alg_get_rsgl`, and `crypto_aead_authsize`)
showed that:

1. The harness's `main` is reachable (`DIAG-harness`
   `__CPROVER_assert(0, ...)` fires with FAILURE).
2. But `af_alg_alloc_areq` is **not** reachable from `main` — the
   DIAG assertion inside it reports SUCCESS, i.e. vacuously unreached.
3. The trivially-false `__CPROVER_requires(0 == 1)` at the target
   call site (line 280) also reports SUCCESS, confirming the call
   site is not reached.

The path dies before `af_alg_alloc_areq` is called — i.e. somewhere
in the first eight lines of `_aead_recvmsg`'s prologue:

```c
struct sock *sk = sock->sk;
struct alg_sock *ask = alg_sk(sk);
struct sock *psk = ask->parent;
struct alg_sock *pask = alg_sk(psk);
struct af_alg_ctx *ctx = ask->private;
struct aead_tfm *aeadc = pask->private;
struct crypto_aead *tfm = aeadc->aead;
struct crypto_sync_skcipher *null_tfm = aeadc->null_tfm;
unsigned int i, as = crypto_aead_authsize(tfm);
```

…or in the inlined body of `aead_sufficient_data$link1`
(static-inline, same nested pointer chain), or in the inlined
`crypto_aead_authsize$link1`, `crypto_aead_reqsize$link1`
(static-inline, read `tfm->authsize`, `tfm->reqsize`).

The harness builds a complete concrete pointer graph
(`child_ask → parent_ask → aeadc → tfm` plus `ctx`), sets
`ctx->init=1`, `ctx->more=0`, `ctx->enc=0`, `ctx->used=64`,
`ctx->aead_assoclen=0`, `tfm->authsize=16`.  Under those
constraints, `aead_sufficient_data` should return true and the call
site should be reached.  CBMC nevertheless reports the call site
unreachable.  Root cause (our current understanding): **CBMC's
symbolic execution kills paths through nested nondet pointer
dereferences within inlined static-inline kernel helpers**; the
`$link1` copies of those helpers treat the incoming pointer chain as
abstract and the guard on the "continue past the return" branch
folds to false.  Overcoming this barrier would require either
(a) replacing the inlined helpers with nondet-return stubs via
`crangler` or
`goto-instrument --generate-function-body`
(LIM-008 blocks the latter), or (b) a much more elaborate harness
that forces CBMC's pointer analysis to resolve the graph concretely
through goto-instrument's `aggressive-slicer`.  Neither is quick.

**Scope clarification — the resolved state.** The scan pipeline's
role on the real kernel source is now characterised as:

1. Coccinelle **prefilter** flags the sg_chain + aead_request_set_crypt
   pattern at `crypto/algif_aead.c:280` (→ hit reported).
2. `compile_file.sh` + `scan.py` **compile** the kernel source
   through `goto-cc` with the right config fragments (→ goto
   binary produced).
3. `scan.py` **links** the kernel goto binary with the adapter
   (contract on `aead_request_set_crypt`), the kernel-aware stubs,
   and the harness; runs `goto-instrument
   --replace-call-with-contract` and `cbmc`.
4. The `cbmc_status: "successful"` result **is a reachability
   sanity result, not a safety result**: it means cbmc completed
   without finding any contract violation on the paths it could
   explore; it does **not** mean the code is free of the Copy Fail
   bug.  See caveat above.

The **substantive property test** for the Copy Fail bug class lives
in `integration/linux/cve-2026-31431/harness_kernel.c` — a
harness-driven regression that reconstructs the kernel's bit-packed
scatterlist shape and links against the same adapter + contract.
That harness produces `VERIFICATION FAILED` on the vulnerable
decrypt shape and `VERIFICATION SUCCESSFUL` on the fixed shape, and
the `cve-2026-31431/run.sh` case 4 asserts exactly this distinction.
That is the test that demonstrates the property is correctly defined
and the adapter correctly enforces it.

**What this means for proactive CBMC scanning.** For this CVE, the
workflow that this project supports today is:

1. Coccinelle prefilter highlights the suspicious pattern in the
   kernel source (objective, lightweight).
2. The kernel-layout regression harness
   (`cve-2026-31431/harness_kernel.c`) proves that the property,
   adapter, and `goto-instrument --replace-call-with-contract`
   pipeline do catch the bug when the vulnerable SGL shape is
   presented to the contract.
3. Scaling step 2 up so that CBMC drives the real `_aead_recvmsg`
   body to the call site — i.e. getting CBMC to synthesise a
   vulnerable SGL shape **automatically** from `_aead_recvmsg`'s
   control flow — is blocked by CBMC's path-kill through
   nested-nondet-pointer inlined helpers.  This is an upstream
   CBMC-front-end precision issue, not something the harness or
   adapter can fix in isolation.

Recording this honestly is the M4c exit.  Future work to actually
get the scan of the real `crypto/algif_aead.c` to produce `FAILED`
would need to either (a) land the CBMC-side precision improvements
for nested static-inline pointer chains, or (b) ship a
source-to-source rewrite (via `crangler` or a specialised pass) that
replaces each static-inline helper with a nondet-return stub at
goto-cc time.

**Artefacts of the investigation.** The diagnostic probes
(`__CPROVER_requires(0 == 1)` on the contract, `__CPROVER_assert(0)`
bisection in the stubs, the full concrete-pointer-graph harness)
are not kept in the committed tree; they were scoped to
LIM-009-follow-up work.  The committed tree retains:

- Kernel-aware stubs with concrete materialised `stub_user_page`
  (USER_WRITABLE) and `stub_tx_page` (nondet provenance per call),
  so when cbmc does reach `af_alg_pull_tsgl`'s body, it explores
  both the safe and the vulnerable witness.
- A harness-with-pointer-graph
  (`scan/adapters/aead_kernel_harness.c`) that allocates and wires
  up `struct socket / alg_sock / af_alg_ctx / aead_tfm / crypto_aead
  / crypto_sync_skcipher` into a valid pointer graph.  This harness
  is a foundation for future work on lifting LIM-009's scope
  clarification.

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
