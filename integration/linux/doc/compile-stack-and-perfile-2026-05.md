# Compile-stack robustness + per-file synthesis for
# synthetic-checkpoint modules — closeout

**Date:** 2026-05-26

This iteration tackled the two highest-leverage remaining
problems from the previous closeout:

1. **Compile-stack robustness** — the n=200 measurement
   had 64 % of rows error out before CBMC could run.
2. **Per-file synthesis coverage** — the synthetic-
   checkpoint modules (resource_leak_on_error_path,
   null_after_alloc, use_after_free_generic) were not
   wired into per-file synthesis, so they could only
   detect bugs via cocci prefilter or synthetic harnesses.

## Part A: Compile-stack robustness

Categorised the 85 compile errors from the n=200 v1 run
into 35 distinct patterns.  Fixed the four most common:

### 1. `kbuild-modname-dash` (5 cases)

When the source file's name contains a dash
(e.g. `acp-es8336.c`), the synthesizer's mangled
file-local symbol `__CPROVER_file_local_acp-es8336_c_X`
is not a legal C identifier.  Goto-cc itself sanitises
non-identifier characters to underscores when emitting the
symbol, but the synthesizer wasn't doing the same mapping.

**Fix:** mirror goto-cc's mapping in the synthesizer
(`re.sub(r"[^A-Za-z0-9_]", "_", source.stem)`).

### 2. `syntax-bare-semi` (8 cases)

The fallback regex in `find_function_signature` was loose
enough to match function CALLS as if they were
declarations.  When a function called another function in
its body and the called function name was the one we were
looking for, the synthesizer would emit a harness with
empty parameter types.

**Fix:** reject loose-pattern matches whose captured
return type doesn't contain at least one type-like token.
Now correctly returns "function not found" so the case is
classified as `skipped` rather than `error`.

### 3. `BUFFER_FNS` (4 cases)

`<linux/buffer_head.h>` defines `BUFFER_FNS` only inside
`#ifdef CONFIG_BLOCK`.  Our x86_64 allnoconfig-derived
scan trees often have CONFIG_BLOCK off, so `BUFFER_FNS`
is undefined and `<linux/jbd2.h>` (which uses it
unconditionally) fails to parse.

**Fix:** `scan-compat.h` now defines `BUFFER_FNS` and
`TAS_BUFFER_FNS` as no-ops when undefined.  Sound for
goto-cc scans: the resulting `set_buffer_xxx` /
`clear_buffer_xxx` accessors are silently dropped; the
link step will report unresolved symbols if the kernel TU
actually references them.

### 4. Macro-invocation function names (14 cases)

The patch-hunk regex was matching macro invocations
(`EXPORT_SYMBOL`, `DEFINE_PER_CPU`, `TRACE_EVENT`, etc.)
as if they were function definitions.  The synthesizer
then tried to find these "functions" in the source and
either emitted a broken harness or got CBMC errors.

**Fix:** `_parse_patch` now rejects all-caps macro names,
and known macro-name prefixes (`EXPORT_*`, `DEFINE_*`,
`TRACE_*`, `MODULE_*`, `module_*`, `subsys_initcall`,
etc.).  Iterates ALL `@@` hunks in the relevant section
instead of stopping at the first match, so a real function
name later in the patch can win.

### Compile-stack improvement

n=200 measurement before / after the four fixes:

| Metric | n=200 v1 | n=200 v2 |
|---|---:|---:|
| error rows | 85 | 68 |
| cleanly-testable rows | 11 | 14 |
| cleanly-testable CVEs | 10 | 13 |
| Detected | 2 | 2 |
| FP-filtered | 4 | 6 |

## Part B: Per-file synthesis for synthetic-checkpoint modules

Wired three synthetic-checkpoint modules into per-file
synthesis: `resource_leak_on_error_path`,
`null_after_alloc`, `use_after_free_generic`.

These modules differ from the balance / lock-state /
netlink-attr modules in that their bug-class semantics
live entirely inside the function body (allocation here,
missing free on early-return there).  They have no
parameter type to match for ghost bootstrap.

### Synthesizer changes

* New `MODULE_GHOST_BOOTSTRAP` entries with empty `types`
  and `uses_cocci_instrumentation: True`.
* When that flag is set, the synthesizer suppresses the
  "empty-ghost low-confidence" warning that would otherwise
  fire on every per-file invocation of these modules.
* Validator (`cve_validate.py`) auto-sets
  `INSTRUMENT=<module>` when the module has the flag —
  without cocci instrumentation, the per-file harness is
  always vacuous since there's no in-TU contract to check.

### Adapter changes

The synthetic checkpoint functions
(`__assert_no_leak_at_exit`, `__assert_safe_to_deref`,
`__assert_not_freed`) now have empty bodies in their
adapter files so that `goto-instrument
--replace-call-with-contract` can attach contracts to them
during the per-file pipeline.  Previously they were
declared without bodies and contract attachment failed
silently.

### Cocci rule changes

`resource_leak_on_error_path.cocci` was failing with
"inconsistent control-flow paths" on functions with many
returns (e.g. `lima_heap_alloc`), causing cocci to abandon
the WHOLE FILE rather than just skipping the troublesome
function.

**Fix:** split the alternation
`\(kmalloc \| kzalloc \| kcalloc \| ...\)` into one
rule pair per allocator API.  Per-API rules keep cocci's
CFG analysis bounded.  Also added coverage for
`kmalloc_array`, `kvmalloc`, `kvzalloc`,
`kvmalloc_array`.

### Validator changes

`_MODULE_API_PATTERNS` extended so the synthetic-checkpoint
modules are picked as candidate modules whenever the
function uses kmalloc/kfree-family APIs.  With
`--modules-per-cve 3`, each CVE now has up to 3 module
candidates including a synthetic-checkpoint one.

## Final n=200 measurement

After both Part A and Part B improvements:

| Verdict | Count | Unique CVEs |
|---|---:|---:|
| error | 99 | 52 |
| vacuous | 60 | 33 |
| successful | 6 | 6 |
| noise | 6 | 6 |
| fp-filtered | 6 | 6 |
| timeout | 5 | 2 |
| candidate | 3 | 2 |

n=185 verdict rows / 94 unique CVEs.

Cleanly-testable subset:

| State | Count |
|---|---:|
| reverted | 9 |
| already_vuln | 5 |
| upstream_vuln | 1 |
| **TOTAL** | **15** (14 unique CVEs) |

Cleanly-testable verdicts:

| Verdict | Count | CVEs |
|---|---:|---|
| **candidate** | 3 (2 unique) | CVE-2024-27025, CVE-2025-21654 |
| fp-filtered | 6 | known FP shapes correctly classified |
| successful (catalog missed) | 6 | 6 catalog-missed CVEs |

**Recall on cleanly-testable bugs:** 2 / (2 + 6) = **25%**.

This is a slight numerical decrease from the prior **33%**
(2/(2+4)) — but the denominator grew from 6 cases to 8
cases, and detection count stayed flat at 2.  The catalog
didn't surface NEW bugs from the synthetic-checkpoint
modules even though the infrastructure is now in place.

### Module usage breakdown (n=200 v3)

| Module | Verdict rows |
|---|---:|
| use_after_free_generic | 46 |
| resource_leak_on_error_path | 37 |
| null_after_alloc | 27 |
| lock_state | 27 |
| device_lifetime | 14 |
| skb_lifetime | 11 |
| (others) | 23 |

The synthetic-checkpoint modules now account for most of
the verdict rows (110/185 = 59%).  Most of those rows are
**vacuous** (cocci didn't fire because the alloc pattern
didn't match the function body) or **errors** (cocci did
fire but the resulting goto-program crashed CBMC's symex
or hit a structural-equivalence link error).

### Why no new detections?

Two reasons:

1. **Cocci coverage gaps.** The cocci rules for
   resource_leak / null_after_alloc / use_after_free
   match a fixed set of allocator APIs.  Many real CVEs
   use wrapper functions (`devm_kmalloc`,
   `kmalloc_array_node`, custom allocators in driver
   subsystems) that aren't in the rule's alternation.
   When cocci doesn't fire, the per-file scan is vacuous.

2. **CBMC abort on complex kernel TUs.** Several files
   that DID get cocci-instrumented (e.g. lib/argv_split.c
   with kmalloc) caused CBMC to abort with an internal
   invariant violation during symex.  This is an upstream
   CBMC bug that's not specific to the catalog work.

Both are tractable but require further investment.  In
particular:

- Extending the cocci alloc-API list to the 50+ allocators
  the kernel actually uses.
- Triaging the CBMC invariant-violation aborts (likely a
  small number of distinct bugs).

## Honest assessment

**What this iteration confirms:**

* **The compile-stack improvements are real.** Three new
  cleanly-testable cases (lifting from 11 to 14 rows), all
  achieved without methodology changes — just engineering
  fixes to the existing pipeline.
* **The triage filter is robust.** 6/6 of the new
  cleanly-testable non-bugs were correctly classified as
  known FP shapes (escape_via_store, put_only_on_error,
  ownership_handler), keeping the headline candidate list
  clean.
* **The infrastructure for synthetic-checkpoint modules
  IS now in place.** End-to-end synthetic test confirms a
  kmalloc-and-leak case correctly fires the contract.
  Real-kernel detections await further cocci-rule and
  CBMC-symex investments.

**What this iteration didn't confirm:**

* The synthetic-checkpoint modules did NOT lift recall.
  Most of the cocci-fires-on-allocation cases are vacuous
  (no error path detected) or crash CBMC.  Recall measures
  25% (down slightly from 33% on a smaller sample).
* The compile-stack still has many remaining error
  patterns (member-not-found, missing-include,
  incomplete-struct, segfault-134/139) that would each
  need targeted fixes.

## What's left

Honest priorities from here:

1. **Extend cocci alloc-API list.** Add `devm_kmalloc`,
   `kmalloc_array_node`, `kmem_cache_zalloc`,
   `pcpu_alloc`, `dma_alloc_coherent`, etc.  Each new
   allocator unlocks more cases for cocci instrumentation.
   ~1 week.
2. **Triage CBMC invariant-violation aborts.** Catch the
   abort, save the .gb file, and triage the failing inputs.
   May be 2-3 distinct upstream bugs.  ~1 week.
3. **Address remaining compile-error patterns.** Each of
   the 30+ remaining error categories is small (1-4 cases)
   but the long tail adds up.  Some are config-mismatch
   issues that would need per-file CONFIG overrides; others
   are CBMC frontend limitations.

## Reproducing

```sh
ulimit -v unlimited
mkdir -p /tmp/cve-validate
systemd-run --user --scope --quiet \
    --property=MemoryMax=60G --property=MemorySwapMax=0 \
  python3 integration/linux/doc/scripts/cve_validate.py \
    --n 200 --timeout 900 --invert --modules-per-cve 3 \
    --upstream-repo /home/ubuntu/torvalds-linux.git \
    --out-csv /tmp/cve-validate/results.csv
```

The synthetic-checkpoint modules require
`INSTRUMENT=<module>` to produce non-vacuous verdicts; the
validator now sets this automatically.

## Cross-references

- [cve-recall-n200-closeout-2026-05.md](cve-recall-n200-closeout-2026-05.md) —
  the prior closeout that motivated this iteration.
- [path-2a-closeout-2026-05.md](path-2a-closeout-2026-05.md) —
  earlier path-2(a) closeout (n=60 measurement).
- [MODULE_CATALOG.md](../MODULE_CATALOG.md) — the 34-module
  reference document.
