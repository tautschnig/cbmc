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

## LIM-008 — `goto-instrument --generate-function-body` fails silently on broad regexes [RESOLVED]

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

**Resolution (commit c86617c94c).** Investigated under bounded
`ulimit`/`timeout` and found two distinct bugs:

1. **O(N²) in `symbol_table_baset::next_unused_suffix`.**  The
   C object factory's `get_fresh_aux_symbol` restarted the linear
   scan from 0 on every allocation, so N allocations cost O(N²)
   total.  Fixed by moving `symbol_table_buildert`'s per-prefix
   hint-cache up into the base class, making the one-arg
   `next_unused_suffix(prefix)` amortised O(1).

2. **Runaway tree size in `symbol_factoryt::gen_nondet_init`.**
   The existing `max_nondet_tree_depth` cap only fires when the
   same struct tag appears twice on a pointer chain; kernel
   hierarchies are wide-but-non-recursive, so the cap never
   triggered and the factory generated an exponentially large
   init body (what produced the "silent no-op" / OOM-then-exit
   from the failure-mode description above).  Added a new
   `max_dynamic_object_instances` parameter (default 1000; CLI
   flag `--max-dynamic-object-instances`) that hard-caps the
   total allocations any single nondet-init root produces.
   Beyond the cap, pointers are initialised to NULL.

The original LIM-008 reproducer (havoc body for
`af_alg_alloc_areq` on Linux 5.10's `crypto/algif_aead.c` goto
binary) completes in 2 seconds after this commit instead of
hanging indefinitely.  Regression covered by
`regression/goto-instrument/generate-function-body-deep-struct-cap/`.

## LIM-010 — Stub fidelity: fix-direction regression not passing [RESOLVED]

**First hit:** attempted fix-direction regression after LIM-009 was
resolved.  With a synthetic fix applied to
`crypto/algif_aead.c` (both `sg_unmark_end` and `sg_chain` removed
from the decrypt-with-chain block), scan.py **still** reports
`cbmc_status: "failed"` with `precondition.3` fired — even though
the walker, on paper, should terminate cleanly at
`sg[0].page_link & SG_END`.  The counterexample trace shows cbmc
walking two SGL entries via a phantom `SG_CHAIN` bit pattern, then
dereferencing a `NULL + N` pointer the harness never placed.

**Root cause (probable at time of filing).** Incomplete stub
fidelity in `scan/adapters/aead_kernel_stubs.c`: several kernel
helpers the decrypt-with-chain path calls on its way to
`aead_request_set_crypt` are still unlinked externs and return
nondet values that corrupt the SGL array cbmc then walks through
the contract predicate.

**Actual root cause (LIM-012).** The fix-direction regression
could not be made to pass because the pipeline itself was not
soundly end-to-end — the scan's verdict on the vulnerable
direction was also partly driven by nondet-stub havoc, not
exclusively by the Copy Fail shape.  See LIM-012.

**Resolution.** LIM-012 path (2): switch the harness to call
`aead_request_set_crypt` directly with a hand-built SGL shape
(`scan/adapters/aead_kernel_direct_harness.c`), selecting between
vulnerable and safe shapes with a compile-time `-DFIXED` flag.
`scan.py --direction=fix` builds the harness's safe branch and
reports `cbmc_status: "successful"`; the default direction
builds the vulnerable branch and reports `cbmc_status: "failed"`
with `precondition.3` fired at the harness's call site.
`scan/run.sh` case 4 gates on this.  The kernel source is still
compiled and linked, so the required-bodies guardrail continues
to catch LIM-009-style static-linkage failures.

**Trade-off.** The scan no longer claims to autonomously
synthesise vulnerable inputs from the full kernel control flow
— that claim was never actually delivered soundly (LIM-012).
The new pitch is: Coccinelle prefilter finds candidate sites;
direct-call harness verifies the contract catches the bug class
on kernel-layout inputs; required-bodies + vacuity guardrails
confirm the linked binary is well-formed.  This generalises
cleanly to every subsequent module.

## LIM-011 — goto-cc link conflict on `static inline` kernel helpers across kernel versions [RESOLVED]

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

**Resolution.** Applied the Phase 3.1 fix: deleted
`scan/adapters/aead_kernel_stubs.c` (which was the only remaining
adapter file that pulled in `<crypto/if_alg.h>` and `<net/sock.h>`,
both of which transitively include `<linux/pagemap.h>` on 5.12+).

Under the LIM-012 path-2 direct-call harness, the kernel TU's
`_aead_recvmsg` body is never called from `main`, so the kernel
externs that stubs file was providing bodies for are unreachable
in cbmc's analysis.  goto-cc resolves them as nondet-return
stubs automatically; CBMC does not explore them.  The stubs file
was dead weight whose only effect was pulling cross-TU static
inlines in and triggering the LIM-011 conflict.

After the fix, the 5.12-rc3 smoke test reports all three targets
(`crypto/algif_aead.c`, `fs/splice.c`, `lib/iov_iter.c`) as
`cbmc_status: "failed"` with their respective preconditions
named, matching the 5.10 behaviour exactly.  The scan pipeline
is now validated on two kernel versions.

## LIM-012 — scan verdict is non-monotonically dependent on `slice_preserve` [RESOLVED]

**First hit:** Phase 1 of the follow-up plan to LIM-010, when the
fix-direction regression kept reporting FAILED and the
investigation dug into why.

**Finding.** On the exact same kernel source and the exact same
adapter + stubs + harness, the scan's `cbmc_status` depends on
which stub bodies are in the `slice_preserve` list in a
non-monotonically-intuitive way.  A bisection on
`crypto/algif_aead.c` (unmodified Linux 5.10) with
`--aggressive-slice` gave:

```
preserve predicates only                           -> vacuity-risk
+ af_alg_alloc_areq                                -> failed   (*)
+ af_alg_get_rsgl (no alloc_areq)                  -> successful
+ af_alg_alloc_areq + af_alg_get_rsgl              -> timeout
+ all three aead stubs                             -> successful
```

(\*) is the historic LIM-009 end-to-end verdict the project has
been claiming is the "Copy Fail detection".

The problem is not one of these results being "right" — it is
that **the scan is sensitive to a configuration knob whose effect
on soundness is not understood**.  Different combinations of
preserved bodies carve out different reachable-state space
approximations, and the solver's verdict on the contract
precondition follows.  The "failed" result we gate the scan
regression on (`scan/run.sh` case 2) is partly driven by cbmc
walking nondet bits in `first_rsgl.sgl.sg` left there when
`af_alg_get_rsgl`'s stub body is sliced away — not purely by the
Copy Fail scatterlist shape.

**What this means.**

- The LIM-009 "resolved" claim is **partially correct**: the scan
  does report FAILED on the vulnerable kernel source, and the
  vacuity probe correctly confirms the call site is reached.  The
  guardrails do their job.
- But the FAILED is not **exclusively** caused by the Copy Fail
  shape; some of the failure is from nondet-return stubs
  poisoning the scatterlist.  A sufficiently complete, aggressive
  fix of the `slice_preserve` list and stub bodies would change
  that verdict in either direction.
- A proper fix-direction regression (LIM-010) is therefore
  impossible under the current pipeline design — because the
  vulnerable-direction is itself not a clean reflection of the
  property.

**Resolution direction.** Two options, substantive work each:

1. **Stop relying on aggressive-slice** and instead route the
   scan through `--reachability-slice` plus a hand-authored list
   of bodies to remove.  Gives the operator direct control over
   what gets nondet-stubbed; removes the subtle
   preserve-list-vs-verdict coupling.  Scales worse (every new
   module needs its own list) but is predictable.

2. **Replace the through-kernel-control-flow scan with a
   direct-call harness pattern** (the `cve-2026-31431/
   harness_kernel.c` pattern, promoted to
   `scan/adapters/aead_kernel_direct_harness.c`): construct a
   known-vulnerable and a known-safe SGL shape in C, call
   `aead_request_set_crypt` directly, exercise the contract.
   Gives up the "CBMC synthesises the vulnerable input
   automatically from the kernel body" claim in exchange for
   predictable, sound end-to-end verdicts.  Generalises cleanly
   to every subsequent property module.

Either is a day or two of focused work.  Option (2) is what the
blog-post draft already implicitly falls back to when it talks
about the "kernel-layout regression pattern" — aligning the
day-to-day scan pipeline with that pattern would let us close
LIM-010 and LIM-012 together.

**For now.** `scan/run.sh` case 2 still gates on FAILED with
precondition.3 fired.  The guardrails from commit 6ae97dbf48
continue to catch the LIM-009-style regressions (broken harness
→ `vacuity-risk`).  Case 4 (fix-direction) remains open pending
either of the resolution paths above.

**Resolution (same commit that files this).** Path (2) from the
"Resolution direction" above was pursued: the scan's harness was
replaced with a direct-call harness at
`scan/adapters/aead_kernel_direct_harness.c`, which constructs a
kernel-layout scatterlist shape explicitly (vulnerable or safe,
switched by `-DFIXED`) and calls the contract target directly.
The kernel source is still compiled and linked, so the
required-bodies guardrail continues to catch LIM-009-style
static-linkage failures, but `_aead_recvmsg`'s body is no longer
on the contract's reachability chain.

Consequences:

- `slice_preserve` no longer needs any stub bodies preserved —
  only the predicates referenced from contract `__CPROVER_requires`
  clauses.  The non-monotonic configuration dependence is gone.
- `scan.py` gained a `--direction={vuln,fix}` flag that is wired
  into compile_file.sh as a `-D` passthrough for the harness.
- `scan/run.sh` case 4 (fix-direction) now passes — LIM-010
  closed in the same stroke.
- The "CBMC synthesises the vulnerable input from the kernel
  body" pitch is retired.  The scan's honest value proposition
  is now: Coccinelle prefilter finds candidate sites; direct-call
  harness verifies the contract catches the bug class on
  kernel-layout inputs; required-bodies + vacuity guardrails
  ensure the linked binary is well-formed.  This pattern
  generalises to M5b (pipe_buffer kernel adapter) and every
  subsequent module.

## LIM-013 — scan cannot currently give per-file bug verdicts [RESOLVED]

**First hit:** Phase 2.3 corpus experiment (see
`doc/corpus.md`).  Running the scan against a 13-file corpus of
aead- and pipe_buffer-relevant kernel files produced the same
`cbmc_status: "failed"` verdict on every file that compiled and
linked cleanly.

**Why.** The LIM-012 path-2 resolution replaced the
through-`_aead_recvmsg` scan with a direct-call harness.  The
harness builds a kernel-layout vulnerable scatterlist / pipe_buffer
shape explicitly and calls the contract target — and it is the
same harness regardless of which kernel `.c` file is compiled
alongside it.  Consequently:

- Every file that compiles + links + reaches the contract site
  will report `failed` on `--direction=vuln` and `successful` on
  `--direction=fix`.  The verdict is a **property-module
  self-check**, not a per-file signal.
- The per-file signal the scan does produce is the Coccinelle
  prefilter hit list — a textual match of the bug-class
  signature.  That is still genuinely useful (it's what finds
  the candidate files to review), but it's a weaker signal than
  "CBMC proves this file is buggy."

This was implicit in the LIM-012 resolution — retiring the
autonomous-shape-synthesis pitch — but the corpus experiment
makes it concrete.  The scan's honest output today is:

> "Coccinelle flagged N files under prefilter X.  Our property
> module's contract catches bug class X on kernel-layout inputs.
> Here are the N files; please review them manually."

**Workaround.** None at this layer.  To get a per-file verdict,
the pipeline would need a per-file harness that constructs an
input shape *derived from that file's control flow*.  That is
exactly what the original through-`_aead_recvmsg` harness tried
to do — and LIM-012 is the limitation we hit trying to make that
sound.

**Resolution direction.**

1. **Per-file targeted harness generation.**  For each prefilter
   hit, emit a small harness that invokes the specific function
   in the target file that produced the hit, with arguments
   constructed from that function's signature.  This is the
   "guided fuzzer" or "harness synthesis" direction — substantial
   work, and still bounded by the same slicer pathology that
   blocked LIM-012 once the call graph gets deep.

2. **Coccinelle-only scan with CBMC as property-correctness
   gate.**  Accept that the scan's per-file output is the cocci
   hit list, and reposition CBMC's role as "validating that the
   property module catches the bug class, on the kernel's exact
   struct layout, every time we change the property code."
   Simpler, sound, and honest about its limits.  This is where
   the current pipeline naturally lives.

3. **Sound whole-program scan.**  A full resolution would mean
   building an analysis that can both (a) reach the contract
   site through the kernel's control flow and (b) be robust to
   slicing choices.  This is the direction of future CBMC
   front-end work — see LIM-008 (`--generate-function-body`
   reliability) and the uncomitted goto-symex optimisation
   backlog.

For now, the scan's regressions (`scan/run.sh` cases 1–6) are
sound in what they test, and `doc/corpus.md` calls out the
per-file limitation explicitly.

**Partial progress (task 5 investigation).** The goto-harness
tool (`src/goto-harness/`) generates exactly the shape we'd
need: `goto-harness --harness-type call-function --function
do_coredump` emits a synthetic main that calls `do_coredump`
with nondet parameters.  Combined with the LIM-008 allocation
cap (which keeps nondet-struct-init bounded), this produces a
useful per-file harness in seconds on real kernel TUs.  The
remaining blocker is LIM-016 (below) — the DATA_INVARIANT in
`get_contract` fires when the contract-declaration has
`__CPROVER_requires` clauses but the kernel TU's re-declaration
doesn't.  That bug is in `src/goto-instrument/contracts/`
contracts.cpp:593`; fixing it unblocks the full LIM-013
resolution.

**Fully resolved.**  LIM-016's two parts — the metadata-strip
in contracts.cpp and the adapter struct forward-decl refactor —
landed together with a hand-rolled harness synthesiser that
bootstraps the property module's ghost state for the nondet
arguments.  The pipeline now lives in two new scripts:

- `scan/synthesise_harness.py`: given `(module, kernel-file,
  target-function)`, parses the target's signature from
  source, forward-declares the struct types it touches,
  emits a C harness that nondet-allocates each pointer
  argument (1 KiB static backing so the byte size doesn't
  depend on the kernel struct's actual layout), bootstraps
  the property-module ghost state for pointer types the
  module tracks, and calls the target.
- `scan/scan-per-file.sh`: the outer driver.  Compiles kernel
  TU + synthesised harness, links with the adapter + property
  module, applies the contract(s) with
  `goto-instrument --replace-call-with-contract`, and runs
  cbmc on the harness entry.  Exits 0/10 for
  SUCCESSFUL/FAILED.

And `scan/test-per-file.sh` is the regression that exercises
the full flow on a real kernel target: Linux 5.10
`fs/nfsd/auth.c`'s `nfsd_setuser`, which has two back-to-back
`put_cred` calls at the end (lines 85–86).  cbmc reports
`VERIFICATION FAILED` with `cred_live.precondition` firing at
both call sites — a per-file signal that could not be produced
before.

The direct-call regressions (case 2 / 5 / 6 in scan/run.sh)
continue to cover the "does the contract catch the bug-class
shape" question; the per-file flow complements them with
"does this specific kernel function's control flow reach the
contract target on a state the contract rejects".

## LIM-014 — goto-cc constant-folding pathologies on Linux 6.x headers [RESOLVED]

**First hit:** Phase 2 task 4 (validate pipeline on a recent LTS).
Building `crypto/algif_aead.c` from Linux 6.6 under `goto-cc`
aborted with:

```
./include/linux/find.h:63:1: error: expected constant expression,
  but got '-(size + 18446744073709551615ul >= offset ? 0 : 1)'
```

**Root cause.** Several Linux 6.x headers use new static-assert
idioms that CBMC's front-end does not constant-fold correctly:

1. `GENMASK_INPUT_CHECK(h, l)` in `<linux/bits.h>` wraps
   `BUILD_BUG_ON_ZERO(__builtin_choose_expr(__is_constexpr((l) >
   (h)), (l) > (h), 0))`.  CBMC mis-evaluates
   `__is_constexpr(…) * 0l` as a null-pointer-constant even when
   the argument is runtime, then selects the `(l) > (h)` branch
   and complains it's not a constant expression at
   `BUILD_BUG_ON_ZERO` time.
2. `__cacheline_group_begin_aligned(...)` in `<linux/cache.h>`
   expands to `__aligned((__VA_ARGS__ + 0) ? : SMP_CACHE_BYTES)`,
   using GCC's `?:` with missing middle operand inside an
   alignment attribute.  CBMC's front-end aborts with a
   `gcc_conditional_expression` irep dump.

**Workaround.** New header
`scan/fragments/scan-compat.h` preempts both macros with
sound-but-loose overrides:

- `GENMASK_INPUT_CHECK(h, l)` → `0` (matches the kernel's own
  `__ASSEMBLY__` fallback).
- `__is_constexpr(x)` → `0` (forces runtime-expression branch).
- `__cacheline_group_begin_aligned(GROUP, ...)` →
  `__cacheline_group_begin(GROUP) __aligned(SMP_CACHE_BYTES)`
  (plain alignment, no `?:` shape).

Each override forces inclusion of the originating header first
so the kernel's definition runs, then `#undef`'s and redefines.
The header's own include guard then silences subsequent
re-inclusion.

`scan/compile_file.sh` passes `-include $SCRIPT_DIR/fragments/
scan-compat.h` after the kernel's `-include kconfig.h / compiler_
types.h`.  The `SCAN_COMPAT_H` environment variable can override
the path if a future kernel needs a different compatibility set.

**Validated.** With the workaround, `crypto/algif_aead.c`,
`fs/splice.c`, and `fs/pipe.c` on Linux 6.6 compile cleanly, and
the `scan/smoke-newer-kernel.sh` run reports `cbmc_status:
failed` with the expected precondition firing on 6.6 — the
pipeline is now soundly end-to-end on both 5.10 and 6.6.

**Resolution direction.** Proper fix is upstream CBMC work on
the `__is_constexpr` / `__builtin_choose_expr` constant-folding
logic in the ansi-c front-end.  Filed as a candidate for
upstream contribution; tracked in
`integration/linux/doc/upstream-contributions.md`.

**Resolution.** Fixed upstream in
`src/ansi-c/c_typecheck_expr.cpp`'s `typecheck_expr_trinary`.
Root cause: when deciding whether one operand of a
conditional operator was a null pointer constant, the
type-checker simplified the operand first and then called
`is_null_pointer()` on the simplified result.  That masked
the C-standard distinction between an "integer constant
expression with value 0" and "a runtime expression that
simplifies to 0":

```
__is_constexpr(x) :=
  (sizeof(int) == sizeof(*(8 ? ((void *)((long)(x) * 0l))
                             : (int *)8)))
```

For runtime `x`, `(long)(x) * 0L` simplifies to 0, but the
original expression is not an integer constant expression, so
the whole `(void *)(…)` is NOT a null pointer constant.  The
ternary's composite type is therefore `void *`, dereferencing
that is `void`, and `sizeof(*…) != sizeof(int)` — so
`__is_constexpr(x) == 0` for runtime `x`.  Previously CBMC
missed this distinction and reported `__is_constexpr(x) == 1`
for every `x`, breaking kernel headers that rely on this
macro to select between constexpr and runtime branches.

Fix: before checking `is_null_pointer()` on the simplified
operand, verify the *pre-simplification* operand contains no
non-constant leaves (no `symbol_exprt` / `side_effect_exprt`
/ `function_application_exprt` / `dereference_exprt`).  The
check composes: simplify-to-0 + no-non-constant-leaves ⇒
genuine null pointer constant.

Validated: a standalone reproducer (`__is_constexpr(5)` and
`__is_constexpr(argc)`) now verifies that the first is 1 and
the second is 0.  All 98 CBMC CORE regressions pass.  Linux
6.12's `crypto/algif_aead.c` compiles end-to-end *without*
the `__is_constexpr` workaround in scan-compat.h (which this
commit removes).  Other scan-compat.h overrides
(`GENMASK_INPUT_CHECK`, `__cacheline_group_begin_aligned`,
`__must_be_cstr`) remain in place — they address separate
kernel idioms that the front-end still doesn't handle.

## LIM-015 — further 6.x build failures on specific files [RESOLVED]

**First hit:** Phase 2 task 4 corpus check on Linux 6.6 after
LIM-014's workarounds landed.  Two files that build on 5.10
still fail on 6.6:

1. `lib/iov_iter.c`:
   ```
   error: redeclaration of '_copy_to_iter::1::2::1::3::1::1::1::1::
   __UNIQUE_ID_x_303' with no linkage
   ```
   Triggered by some combination of `_Generic` / `_Static_assert`
   / nested `__UNIQUE_ID` that CBMC's name-mangling mangles
   identically for two distinct sub-expressions.

2. `fs/coredump.c`:
   ```
   error: expected constant expression, but got '{ .lock={ .raw_lock={ } },
     .interval=1250, .burst=10, ...
   ```
   A rate-limit struct initializer that 6.6 now expects to be a
   constant expression context but whose body CBMC cannot fold
   (similar shape to LIM-014 but inside a struct literal).

Neither blocks the overall pipeline: both files are *also*
covered by the aead / cred_lifetime modules respectively
through other kernel TUs that do compile on 6.6
(`crypto/ccm.c`, `fs/coredump` → `kernel/cred.c` etc.).

**Resolution direction.** Unblock each on a case-by-case basis
by extending `scan/fragments/scan-compat.h` with specific
overrides, or by filing focused CBMC front-end PRs per
idiom.  Left open.

## LIM-016 — `goto-instrument --replace-call-with-contract` invariant violation on signature mismatch [RESOLVED]

**First hit:** Phase 2 task 5 investigation of per-file harness
generation.  Using `goto-harness --harness-type call-function` to
synthesise a harness that invokes `do_coredump` from
`fs/coredump.c` (the cocci-flagged put_cred site), then running

```
goto-instrument --replace-call-with-contract \
    __CPROVER_file_local_cred_h_put_cred harness.gb out.gb
```

triggers an invariant violation at
`src/goto-instrument/contracts/contracts.cpp:593`:

```
--- begin invariant violation report ---
Invariant check failed
File: src/goto-instrument/contracts/contracts.cpp:593
  function: get_contract
Condition: type == function_symbol.type
Reason: front-end should have rejected re-declarations with a
  different type
```

**Root cause.** `get_contract` compares the contract-declaration
symbol's type with the function-declaration symbol's type via
`irept::operator==`, which recurses into every sub-irep.  The
contract-declaration has `spec_requires` / `spec_assigns` sub-
ireps attached to its `code_typet` (by virtue of the
`__CPROVER_requires(...)` / `__CPROVER_assigns()` attributes in
the adapter source); the plain function declaration from the
kernel TU does not.  The two types are structurally different
and the DATA_INVARIANT fires, even though the *signature*
(return type + parameter types + parameter names) matches
exactly.

**Why the direct-harness scan still works.**  `scan.py` applies
each contract name in its own goto-instrument invocation with
`check=False` in the `_run` wrapper (see
`integration/linux/scan/scan.py` around the `CONTRACT_FUNCTIONS`
iteration).  For the direct-call harness, only ONE of the two
names in `CONTRACT_FUNCTIONS['cred_lifetime']` actually has a
body-present call site in the linked binary; the other
crashes but is silently tolerated.  In the per-file harness
path every kernel TU we target has the static-inline-mangled
name present, so the crash is unavoidable.

**Workaround scoped for the direct-call harness.**
`scan/adapters/cred_kernel_adapter.c` matches the kernel's
`<linux/cred.h>` signature exactly — parameter TYPE (`const
struct cred *`) and parameter NAME (`_cred`) — to defer the
DATA_INVARIANT until the contract sub-ireps are the only
difference.  This hardens the existing scan pipeline against
future fallout from the silent-tolerance of the
`check=False`-wrapped crash.  Per-file harnesses remain
blocked.

**Resolution direction.** CBMC upstream fix: `get_contract` in
`src/goto-instrument/contracts/contracts.cpp` should compare
the code_typet's structural fields (return type, parameter
types, parameter names) without also comparing the attached
contract clauses.  Cleanest is a new helper
`code_typet::structurally_equal(other)` that strips
`spec_requires` / `spec_ensures` / `spec_assigns` / `spec_
frees` sub-ireps before comparing; `get_contract`'s
DATA_INVARIANT then switches to the structural comparison.
The existing `==` semantics on `code_typet` remain useful
elsewhere (e.g. to detect genuine re-declarations with
different signatures).

**Status.** PARTIAL.  Partial upstream fix landed in commit
9c17432e1a — `get_contract` now strips the known irrelevant
metadata (`#source_location`, `#identifier`, `#base_name`,
`spec_requires` / `spec_ensures` / `spec_assigns` / `spec_frees`)
from both types before comparing.  Contract-only sub-irep
differences no longer trigger the invariant.  All CORE
contracts tests and all nine integration/linux regressions
remain green.

**Remaining:** the property module's adapter declares
`struct cred` with a MINIMAL layout (two fields: `usage` +
padding); the kernel TU's declaration comes with the FULL
struct definition (hundreds of fields).  After linking these
have different `struct_tag` bodies, so even with metadata
stripped the two types are not structurally equal — and that's
a legitimate semantic mismatch rather than a CBMC bug.

**Fully resolved (commit 79e985988c).**  The cred_lifetime
adapter, probe, and direct harness now forward-declare
`struct cred` without a body.  The property module's public
header does the same.  Abstract callers (unit test, CVE
reduction) provide their own local `struct cred { int
dummy; };` for stack allocation; the scan pipeline gets the
kernel's full definition at link time.  With the struct-body
mismatch eliminated and commit 9c17432e1a's metadata strip
in place, `goto-instrument --replace-call-with-contract`
succeeds cleanly on the per-file-harness path: goto-harness
synthesises a harness, the contract installs, and cbmc runs
per-function verdicts.
