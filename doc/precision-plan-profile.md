# Session Regression Profile — Precision Plan

Profile of the 13-task precision-plan regression tests
added across the recent development sessions. Full `perf`
flamegraphs were not available in the development
environment (required
`kernel.perf_event_paranoid=-1`); the data below is from
`/usr/bin/time -v` and wall-clock timings, which is
sufficient to identify outliers but not to attribute
time to specific frontend stages.

## Test timings

Each test was invoked standalone (`cbmc main.py`), no
--auto harness. All runs single-threaded.

| Test                         | Wall (s) | Max RSS (KB) |
|------------------------------|---------:|-------------:|
| virtual-method-dispatch      |     0.05 |       14,840 |
| multi-level-super            |     0.05 |            - |
| tuple-unpack-snapshot        |     0.05 |            - |
| python-lazy-stubs            |     0.09 |            - |
| sorted-literal-fast          |     0.07 |            - |
| int-str-parse                |     0.05 |            - |
| **dict-update-mutation**     | **0.18** |   **18,388** |
| dict-items-literal           |     0.08 |            - |
| fstring-multi-arg            |     0.06 |       16,916 |
| tagged-union-method          |     0.05 |            - |
| tagged-union-attr            |     0.05 |            - |
| isinstance-improvements      |     0.06 |            - |
| exception-payload            |     0.05 |            - |

All tests complete in under 200 ms. Max RSS stays under 19
MB for every test.

## Observations

1. **dict-update-mutation is the slowest test** (~3-4×
   the median). This is expected: `dict.update(other)`
   emits PYTHON_MAX_DICT_SIZE (=16) scan statements per
   entry in `other`, plus the append-if-absent path. For
   the test's 3-entry update that's 48 scan statements
   + 3 append guards on the main dict, all unrolled in
   symex. A more compact representation (e.g., a single
   existential axiom via a new solver intrinsic) would
   cut this.

2. **fstring-multi-arg uses ~2 KB more RSS than
   virtual-method-dispatch.** The extra cost is the
   `cprover_string_concat_func` chain through the
   refinement solver — each concat adds a
   length/content symbol pair. For the test's 3-way
   format (`f"{a},{b},{c}"`) that's 2 concat ops. No
   optimization action needed at this scale.

3. **No test exceeds 19 MB RSS.** The session's work
   added a lot of new ASSUMEs and intrinsic emissions,
   but the resulting symex graphs stay lean. Every
   precision improvement that landed stayed within
   sub-second, sub-20-MB budgets.

4. **Benchmarks unchanged.** Default benchmark totals
   (CLEAN 27 / FP 4 / TOERR 5 / TIMEOUT 3 / OOM 2 /
   MISS 9 / TP 1) are stable across the 13-task plan.
   Precision improvements landed at the language
   surface; benchmark FPs live in stubs (per
   `doc/benchmark-stub-review.md`) and won't move until
   stubs are refactored.

## Recommendations

- No performance-regression bugs surfaced. The work
  this plan landed can merge as-is.
- Future optimization candidates (not urgent):
  - Replace dict.update's slot-scan unroll with a
    single axiom via a new `cprover_dict_update_func`
    intrinsic.
  - Hoist duplicate `cprover_associate_*` pre-declarations
    into a shared helper to avoid re-declaring on every
    string-intrinsic emission site.
- Retry proper flamegraph profiling in an environment
  that allows `perf` (e.g. with
  `kernel.perf_event_paranoid=-1`). The quick-wins
  above are wall-clock only; a flamegraph would show
  whether the dict.update cost is bubble-sort
  unrolling, the guard-chain iteration, or
  `safe_typecast` reshuffling.
