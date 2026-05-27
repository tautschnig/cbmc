# DIFF cluster analysis — wave 41 (post-inheritance fix)

Snapshot: 2026-05-27 wave 41 sweep. **2605 PASS, 399 DIFF, 71
FAIL, 10 TIMEOUT, 3 TOERR, 2 UNKNOWN** out of 3091 tests.

## Direction split

Of the 399 DIFFs:

| Direction | Count | Meaning |
|---|---:|---|
| FAILED expected, SUCCESSFUL actual | 49 | Soundness gap — we miss real bugs |
| SUCCESSFUL expected, FAILED actual | 350 | Precision gap — false positives |

Soundness gaps need fixing first by priority. Precision gaps are
less urgent but more numerous.

## Soundness gaps (49)

The most impactful clusters of missing-bug-detection tests:

| Cluster | Count | Notes |
|---|---:|---|
| `type-annotation-*` | 7 | Use `--is-instance-check` flag (ESBMC-specific). The CBMC equivalent is `--python-check-annotations`. Sweep doesn't yet alias the flag. **Quick win**: add the alias in `scripts/cbmc_on_esbmc_python.py`. |
| `missing-return*` | 7 | Use `--incremental-bmc`. CBMC has no direct equivalent; needs separate logic to detect functions that should return but fall through. |
| `github_3287*` | 3 | Three-test cluster on a single bug. |
| `github_3093*` | 2 | Two-test cluster. |
| `github_3647*` | 2 | Two-test cluster. |
| `import-*` | 3 | import-error / import-from-function / import-os2 — likely missing import-related checks. |
| `re*_fail` | 2 | Regex validation; addresses partly via Wave 2. |
| `math_edge_*` | 3 | degrees / radians / frexp edge cases. |
| `list-*` | 3 | list-depth-exceed / list_comprehension6 / list_pop11_nondet |
| `input*` | 2 | input1 / input5 |
| Scattered | 17 | One-off misc cases |

**Quick wins**: the `--is-instance-check` alias closes 7
soundness gaps immediately.

## Precision gaps (350) — by feature cluster

| Cluster | Count | Share | Notes |
|---|---:|---:|---|
| `github-other` | 79 | 23% | Scattered specific bugs from the github-issue test suite. |
| `string-ops` | 62 | 18% | Mostly specific string methods (isidentifier, isnumeric, partition, replace, casefold, slice with step, format-* edge cases). |
| `math` | 38 | 11% | math module functions: factorial, edge cases, transcendental. |
| `complex-numbers` | 36 | 10% | complex arithmetic / methods / parsing. |
| `list-ops` | 18 | 5% | list methods: append/clear/depth corners. |
| `regex` | 11 | 3% | Regex-content reasoning. |
| `lambda` | 10 | 3% | Lambda binding / capture edge cases. |
| `github-cluster-30xx` | 7 | 2% | Related github-3013/3093 cluster. |
| `decimal` | 4 | 1% | decimal module precision. |
| `class-attributes` | 4 | 1% | Class-level vs instance attribute resolution. |
| `counter-class` | 4 | 1% | collections.Counter (tuple keys / values method). |
| `builtin-all` | 4 | 1% | `all()` with falsy values (None, 0). |
| `class-basic` | 3 | 1% | class10 / class11 / class12 |
| `exception` | 3 | 1% | exception8 / exception10 / exception_base_class |
| `chr-multibyte` | 1 | 0% | `chr(N)` for N > 127 doesn't yield right unicode chars. |
| `dict-class-methods` | 1 | 0% | `dict.fromkeys` |
| Other | 103 | 29% | Long tail. |

## String-ops sub-clusters (62 tests)

Breakdown of the string-ops cluster:

- `string-isidentifier-*` (6): `str.isidentifier()` model. Currently always-nondet.
- `string-isnumeric-*` (5): `str.isnumeric()` model.
- `string-partition*` (5): `str.partition()` precision.
- `string-replace-*` (2+): `str.replace()` count handling.
- `string-casefold-accent`: case-folding for accented chars.
- `string-format-*` (2): format spec edge cases.
- `string-concat*` (3): concat-loop precision.
- `fstring*` (2): f-string formatting.
- `string-slice-step`: slice with step.
- Others (~30): assorted.

## Recommended actions

In order of leverage:

### High-leverage quick wins (~½ day)

1. **Add `--is-instance-check` alias to `scripts/cbmc_on_esbmc_python.py`**
   to close the 7 type-annotation soundness gaps.
2. **Audit and document the `--incremental-bmc` situation** — decide
   whether to implement an equivalent or just document the
   limitation (the 7 missing-return cases).

### Medium-effort cluster fixes (1–3 days each)

3. **`str.isidentifier` / `str.isnumeric` precision**
   (~10 tests). These methods are likely modelled as nondet.
   Implementing exact constant-folding when the receiver is a
   string literal or has known length would close them.
4. **`str.partition` precision** (~5 tests). Similar shape;
   precise constant fold for literal receivers.
5. **`builtin all/any` with falsy values** (~4 tests). The
   issue is likely that `None` (encoded as the int sentinel
   -4611686018427387904) doesn't compare as falsy.
6. **`class-attributes` (class-level access)** (~4 tests). Class
   attribute access on the class itself rather than an instance.

### Larger pushes (1+ week)

7. **Regex Wave 2 — subject-at-SMT-time refactor** (~1 week).
   Closes the 11 regex precision tests + the documented 11
   wrong-fails in the AWS suite.
8. **Math module edge cases** (~38 tests). Ongoing cluster of
   `@c_intrinsic` integration; each new function needs its
   domain / fold / range annotated.
9. **Complex-numbers precision** (~36 tests). Substantial work
   to model complex arithmetic precisely.

### Long-tail items

10. **github-other (79 tests)**: each is its own bug. Worth
    addressing top 5–10 as time permits but no architectural
    win.

## Methodology note

This clustering used:
- Direction split (FAILED vs SUCCESSFUL verdict).
- Name-prefix grouping for both directions.
- For precision gaps, additional grouping by failing-assertion shape.
- Manual spot-checks of the top clusters.

For deeper investigation, run:

```bash
python3 << 'EOF'
import csv
with open('/tmp/cbmc-wave-final.csv') as f:
    diffs = [r for r in csv.DictReader(f) if r['outcome'] == 'DIFF']
# Filter / cluster as needed
EOF
```
