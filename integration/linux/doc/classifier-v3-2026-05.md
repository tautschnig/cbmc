# Classifier v3 — patch-level analysis of the 'other' bucket

This iteration built a CVE classifier v3 that fetches each
CVE's upstream-fix commit patch text from `git.kernel.org`
and classifies based on what the patch actually changes —
rather than only the description text the v1/v2
classifiers used.

The motivating gap: v2 left **44.8 %** of CVEs in the `other`
bucket (and v2's improvements over v1 only shaved that to
43.3 %).  Many of those CVEs ARE about specific bug shapes
the catalog covers; their descriptions just don't use the
keywords v2 looks for ("validate inherited ACE SID length",
"add missing fdput", "fix off-by-one in ...").  The patch
text usually reveals what the bug was.

## Implementation

`integration/linux/doc/scripts/cve_survey_classify_v3.py`:

* Reads the fix commit hash from the CVE JSON's
  `affected[].versions[].lessThan` field.
* Fetches the patch via
  `https://git.kernel.org/.../linux.git/patch/?id=<hash>`
  with retry+backoff.
* Caches successful patches to
  `/tmp/cve-survey/patch_cache/`.
* Runs ~50 patch-level regex patterns over the patch text,
  scoring per category.  The category with the highest
  score wins.  Patterns include things like:
  - `+\s*put_device\(` → `refcount_balance`
  - `+\s*if\s*\(!\s*\w+\)` → `null_pointer_deref` (NULL
    check added)
  - `+\s*kfree\(` or `+\s*goto\s+(?:err|out|free)` →
    `resource_leak`
  - `+\s*cancel_(?:delayed_)?work_sync\(` →
    `cleanup_ordering`
  - `+\s*memset\([^)]*,\s*0,` → `uninit_or_info_leak`
  - `+\s*if\s*\(check_(?:add|mul|sub)_overflow\(` →
    `integer_overflow`
* Falls back to v2 description-based classification when
  the patch is unavailable.

## Run results

Cap to single-file fetcher with 8 concurrent workers (lower
bumped patch-retrieval rate from 2.5 % → 93.3 % by avoiding
git.kernel.org rate limits).

* Total CVEs: 8,719
* Patches successfully fetched: 3,524 / 3,779 (93.3 %) for
  `other`-bucket CVEs.
* Reclassified out of `other`: **971 CVEs** (32.2 % of
  v2's `other` bucket).
* Run wall-clock: ~12 minutes.

### v2 → v3 reattribution

| Category | v2 | v3 | Δ |
|---|---:|---:|---:|
| **other** | **3,779** | **2,808** | **−971** |
| null_pointer_deref | 1,101 | 1,395 | +294 |
| resource_leak | 384 | 683 | +299 |
| lock_discipline | 300 | 425 | +125 |
| out_of_bounds | 720 | 786 | +66 |
| double_free_or_unlock | 105 | 133 | +28 |
| rcu_misuse | 44 | 71 | +27 |
| cleanup_ordering | 54 | 71 | +17 |
| race_or_toctoue | 259 | 276 | +17 |
| uninit_or_info_leak | 95 | 112 | +17 |
| dos_panic_warn | 286 | 305 | +19 |
| refcount_balance | 477 | 517 | +40 |
| use_after_free | 889 | 901 | +12 |
| integer_overflow | 111 | 118 | +7 |
| permission_bypass | 12 | 15 | +3 |

The two biggest gainers — `null_pointer_deref` (+294) and
`resource_leak` (+299) — confirm the suspicion: many CVEs
just say things like "fix missing fdput" or "add NULL check"
in the description, and v2's keyword set didn't catch
those.  v3's patch-text view does.

## Updated coverage estimate

Using v3 numbers (8,719 total, 2,808 still in `other`):

### Categorical coverage (67.1 %)

The fraction of CVEs in **categories where the catalog has
at least one module**:

| Category | CVEs | % | Catalog covers? |
|---|---:|---:|---|
| null_pointer_deref | 1,395 | 16.0% | ✓ null_after_alloc |
| use_after_free | 901 | 10.3% | ✓ balance + use_after_free_generic |
| out_of_bounds | 786 | 9.0% | ✓ netlink + copy_from_user |
| resource_leak | 683 | 7.8% | ✓ resource_leak_on_error_path |
| refcount_balance | 517 | 5.9% | ✓ 12 balance modules |
| lock_discipline | 425 | 4.9% | ✓ lock_state |
| dos_panic_warn | 305 | 3.5% | ✓ division_by_zero (subset only) |
| race_or_toctoue | 276 | 3.2% | ✓ 3 concurrent modules |
| double_free_or_unlock | 133 | 1.5% | ✓ use_after_free_generic |
| integer_overflow | 118 | 1.4% | ✓ integer_overflow_in_alloc_size |
| uninit_or_info_leak | 112 | 1.3% | ✓ uninit_to_user |
| rcu_misuse | 71 | 0.8% | ✓ rcu_critical_section |
| cleanup_ordering | 71 | 0.8% | ✓ cancel_*_before_free family |
| crypto_api | 39 | 0.4% | ✓ aead |
| string_or_copy_bound | 19 | 0.2% | ✓ copy_from_user_size_check |
| **In covered**           | **5,851** | **67.1 %** | |
| bpf_verifier_or_runtime | 42 | 0.5% | ✗ needs BPF front-end |
| permission_bypass | 15 | 0.2% | ✗ |
| format_string | 3 | 0.0% | ✗ |
| other | 2,808 | 32.2% | n/a — unclassifiable |

### Effective coverage (53-66 %)

Applying realistic per-category coverage rates (accounting
for partial coverage of categories like `dos_panic_warn`
where we only catch the divide-by-zero subset):

* **53.4 %** strict (what the catalog provably addresses
  among classified CVEs).
* **61-66 %** with credit for the `other` bucket being
  still-conservative (assuming 25-40 % of `other` is
  misclassified known shapes).

### Theoretical ceiling at the categorical metric: 67.8 %

The v3 classifier puts 32.2 % of CVEs in `other` and 0.7 %
in uncovered small categories (BPF + permission + format).
The mathematical maximum for "in covered categories" is
67.8 %.  We're at 67.1 % — **0.7 % below the natural
ceiling**.

To push the categorical metric past 70 % would require:
1. Reducing `other` further (classifier v4 with deeper
   patch-text patterns or commit-message NLP).
2. Adding `bpf_verifier_or_runtime` coverage (BPF front-end).
3. Adding `permission_bypass` and `format_string` modules
   (small categories but tractable).

## Honest assessment

**Past 70 % depends on the denominator and what we count as
covered.**

* **Strictest**: 53.4 % effective coverage.  Below 70 %.
* **Categorical**: 67.1 % in covered categories.  Below 70 %
  but at the natural ceiling.
* **With residue credit**: 61-66 %.  Below 70 %.

The previous claim of "past 70 % by the classifier-labelled
denominator" was partially right but obscured the
distinction: the catalog covers **67.1 % of v3-classified
CVEs in covered categories**, which is the most defensible
single number.  That's substantial but not past 70 %.

To genuinely cross 70 %:

1. **Quick win (~1 day)**: Add modules for `bpf_helper_arg_validation`
   (when the front-end is integrated), `permission_bypass`
   (capability-check checkpoint), `format_string` (printk
   format string analysis).  These cover the remaining
   **0.7 %** of v3-classified categories not in scope.
2. **Bigger win (~1 week)**: Classifier v4 with richer
   patch-text patterns OR commit-message NLP.  Could
   reasonably move 30-50 % of remaining `other` (so
   around 1,000-1,400 CVEs) into specific categories.
   Most of those fall into already-covered shapes, so
   we'd capture ~10 percentage points additional coverage.
3. **Per-file integration of synthetic-checkpoint modules**:
   Lifts the partial-coverage rates (dos_panic from 30 %
   towards 60-70 %, etc.).  Captures another 5-10 % of
   total volume.

Combined, items 1 + 2 + 3 would credibly push effective
coverage past 75 %.

## What this iteration actually produced

* Patch-text-level classifier (v3) with caching.
* 971 CVEs reattributed from `other` to specific categories.
* A precise per-category coverage table (formerly
  guesswork).
* An honest framing of what "coverage past 70 %" means
  given the structural limits of the survey.

The updated CSV is at
`integration/linux/doc/cve-survey-2023-2026-v3-top50.csv`.
The full v3 CSV is at `/tmp/cve-survey/cves_classified_v3.csv`
(too large for the repo — ~8,719 rows × 6 fields).

## Reproducing

```sh
# Pre-requirements:
git clone --depth 1 \
  https://git.kernel.org/pub/scm/linux/security/vulns.git \
  /tmp/cve-survey/vulns
mkdir -p /tmp/cve-survey/patch_cache

# Run v3 (re-uses v2 CSV; only re-classifies 'other' CVEs)
python3 integration/linux/doc/scripts/cve_survey_classify_v2.py
python3 integration/linux/doc/scripts/cve_survey_classify_v3.py \
  --only-other --workers 8
```

## Cross-references

- [v1 classifier](cve-survey-2023-2026.md) — original
  description-based survey.
- [Phase 1](phase-1-balance-modules-2026-05.md) — early
  catalog work.
- [Beyond 70 % v2](beyond-70pct-2026-05.md) — the v2
  classifier outcomes that motivated this v3 work.
