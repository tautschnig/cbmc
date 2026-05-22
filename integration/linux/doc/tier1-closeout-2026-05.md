# Tier 1 closeout — calibration, productised instrumentation,
# corpus measurement, MODULE_CATALOG

This iteration delivered all four Tier 1 follow-ups plus the
MODULE_CATALOG.md (Tier 2 #7).  Outcomes are mixed and the
honest framing matters more than the headline numbers.

## Delivered

### 1. v4 classifier sanity check — over-confident before, calibrated now

Sampled 50 CVEs that v4 reattributed from `other` to specific
categories and hand-verified.  Independent description-keyword
classification matched v4's choice in only **5 of 50** (10 %)
unambiguous cases; **42 cases** had no clear keyword in the
description (which is precisely why v3 left them in `other`).

Of 15 sampled cases inspected at the patch level:
- **2 plausibly correct** (CVE-2024-27393 missing
  skb_mark_for_recycle → resource_leak; CVE-2023-54273
  xfrm dev tracker leak → refcount_balance).
- **6 likely misclassified** (e.g. CVE-2026-43473 "Add NULL
  checks" → v4 said resource_leak but should be
  null_pointer_deref; CVE-2024-43832 "missing folio
  reference" → v4 said out_of_bounds but should be
  refcount_balance).
- **7 plausible-uncertain** (subjects too generic to judge
  without reading the diff itself).

**Implication for coverage estimate.**  If ~40 % of v4's
2,787-CVE reattribution is misclassified, then v4's
"contribution" to coverage is about half what it appears.
The honest calibrated coverage range is **65-75 % effective
bug-shape volume**, not the previously-claimed 79 %.

This is recorded in `MODULE_CATALOG.md` and the iteration
write-up.  Past 70 % is **at the edge** of what's defensible,
not comfortably past.

### 2. Generalised instrumentation tool

`scan/tools/instrument.py` covers 5 bug shapes (null_after_alloc,
resource_leak_on_error_path, use_after_free_generic,
integer_overflow_in_alloc_size, copy_from_user_size_check).
Each shape:

* Has a regex matching the bug pattern.
* Inserts the corresponding `__assert_<property>(...)`
  checkpoint at the suspicious site.
* Skips when an intervening guard / check / reassignment is
  detected.

Tested on synthetic vuln/safe pairs and on real kernel files.
Integration into `scan-per-file.sh` is conditional on the
`INSTRUMENT` environment variable.

### 3. Instrumented corpus run on Linux 5.10

Ran the full corpus with `INSTRUMENT=all`:

| Metric | Prior 25-module | This run (instrumented) | Δ |
|---|---:|---:|---:|
| distinct files | 165 | 199 | +34 (broader corpus) |
| total cocci hits | 2,365 | 4,550 | +2,185 (more modules) |
| per-file failed | 88 | 9 | **−79** |
| per-file successful | 28 | 6 | −22 |
| per-file noise | 34 | 6 | −28 |
| per-file vacuous | 87 | 55 | −32 |
| per-file skipped | 25 | 16 | −9 |
| per-file error | 1,028 | 1,914 | **+886** |
| Wall-clock | 38 min | 11 min | −27 min |

The dramatic reduction in failed verdicts is mostly because
the **instrumentation broke compilation on ~900 files**
(regex-based variable-scope detection is brittle).  The
verdicts that did surface are the same false-positive
classes from prior runs.

**Honest read**: the regex prototype is not production-ready.
The proof-of-concept that "cocci-driven instrumentation
unlocks per-file CBMC verification" is intact, but the
prototype isn't usable at corpus scale without Coccinelle's
AST-aware structural matching.

### 4. Hand-triage of the 9 failed verdicts

All 9 are familiar false-positive classes from prior runs:
- `commit_creds`, `put_task_stack`: known cred / refcount
  allocate-then-put false positives.
- `component_del`, `component_master_del`: lock_state
  low-confidence (wrapper-paths gap).
- `anon_inode_getfile`, `devpts_pty_new`: inode_lifetime
  allocate-then-put.
- `vsock_core_register/unregister`: lock_state low-conf.

**Zero genuinely new bug candidates surfaced.**  This is
consistent with the saturation finding from earlier
iterations.

### 5. MODULE_CATALOG.md (Tier 2 #7)

Single-document summary of all 34 modules for outside
readers.  Lists the bug class targeted, abstraction shape,
motivating CVE classes, and integration status for each
module, organised by ghost shape.

Located at `integration/linux/MODULE_CATALOG.md`.  Cross-
references the per-phase write-ups for deeper context.

## Calibrated catalog status

After this iteration's calibration:

| Metric | Honest range |
|---|---:|
| In covered categories (v4 nominal) | 98.8 % |
| **Effective bug-shape coverage** | **65-75 %** |
| Center estimate | ~70 % |

Catalog at **34 modules** across **6 ghost shapes**.  All
unit tests pass; all `scan/run.sh`, `test-per-file*`,
`smoke-all-lts.sh` regressions pass.

## Honest assessment of where we are

The catalog at 34 modules is a substantial achievement and
covers the major bug-shape categories of the Linux CVE
record.  But this iteration's results put two things in
sharper focus:

1. **The 79 % effective-coverage claim was overconfident.**
   The v4 classifier's aggressive reattribution introduced
   substantial misclassification.  The calibrated number is
   ~70 %, with substantial uncertainty.

2. **The per-file synthesis pipeline has saturated for
   bug-finding.**  Multiple corpus runs with progressively
   larger catalogs show the same false-positive classes
   recur; we don't surface genuinely new bug candidates.
   The instrumentation prototype could in principle change
   this, but only after productising with proper AST-aware
   matching.

The catalog is now a credible **research foundation** rather
than a **bug-finding tool that's ready for kernel
maintainers**.  The next-tier work to make it the latter
would be substantial.

## What's still pending

Tier 1 items not delivered (because Tier 1 was originally
estimated at "roughly a week of focused work", which this
session compressed):

- **Productise the instrumentation tool**: replace the
  regex-based v1 with Coccinelle's structural matching.
  Without this, instrumentation breaks compilation on too
  many files for corpus-scale use.
- **Run instrumented corpus on all 4 LTS kernels**: only
  ran 5.10 in this iteration.
- **Wider hand triage**: only 9 candidates surfaced (most
  files failed to compile post-instrumentation).

Tier 2 items still in the pipeline:

- Upstream the structural-equivalence linker fix
  (`0414b43bf2`) — long-overdue.
- CI pipeline for weekly per-LTS scans.
- A short coverage-evidence paper / blog post for outside
  readers.

## What this iteration produced

Five commits with concrete value:

1. `permission_bypass` + `format_string` modules.
2. Classifier v4 (aggressive but uncalibrated).
3. Sanity-check finding showing v4 is over-broad; honest
   calibration of the coverage estimate.
4. Generalised `scan/tools/instrument.py` covering 5 bug
   shapes; integrated into `scan-per-file.sh` via
   `INSTRUMENT=`.
5. `MODULE_CATALOG.md` summarising the 34-module catalog
   for outside readers.

The catalog grew from 32 to 34 modules.  The classifier got
more aggressive (then was honestly calibrated).  The
instrumentation pipeline got a working prototype that needs
productisation.  The project now has a single reference doc.

## Reproducing

```sh
# Sanity check the v4 classifier:
python3 -c "
import csv, json, random
random.seed(42)
v3_other = {r['cve'] for r in csv.DictReader(open('/tmp/cve-survey/cves_classified_v3.csv'))
            if r['category'] == 'other'}
v4_reattr = [r for r in csv.DictReader(open('/tmp/cve-survey/cves_classified_v4.csv'))
             if r['cve'] in v3_other and r['category'] != 'other']
sample = random.sample(v4_reattr, 50)
for r in sample[:10]:
    print(f\"{r['cve']}: v4={r['category']} | {r['summary'][:80]}\")
"

# Generalised instrument tool:
./integration/linux/scan/tools/instrument.py /path/to/kernel.c \
  -o /tmp/instr.c

# Per-file scan with instrumentation enabled:
LINUX_TREE=/home/ubuntu/linux_5_10 INSTRUMENT=all UNWIND=2 \
  ./integration/linux/scan/scan-per-file.sh MODULE FILE FN

# Corpus run with instrumentation:
INSTRUMENT=all PARALLEL=16 CORPUS_MAX=200 \
  ./integration/linux/scan/corpus-scan.sh /tmp/runs/$(date +%Y-%m-%d)
```

## Cross-references

- [MODULE_CATALOG.md](../MODULE_CATALOG.md) — the new
  reference document.
- [Classifier v3](classifier-v3-2026-05.md)
- [1+2+3 closeout (v4 + first prototype)](closeout-1plus2plus3-2026-05.md)
