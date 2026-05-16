\file
# Performance analysis: default + cvc5 backends on the AWS Python suite

Profiling the 51-benchmark `python-verification-benchmarks/python-sources`
suite under both `cbmc` (default boolbv backend) and
`cbmc --smt2 --cvc5`, with and without targeted optimizations.

## Setup

- `kernel.perf_event_paranoid = -1` (required for `perf record -e cycles:u`
  with call-graph DWARF).
- `RelWithDebInfo` build at `build-debug/bin/cbmc`.
- `perf record -g --call-graph dwarf,8192 -F 997 -e cycles:u`.
- 8 GB virtual-memory limit per benchmark.

## Suite-level numbers

Both backends with the recommended Python flag suite
(`--object-bits 12 --no-unwinding-assertions --unwind 3
--python-no-exception-checks --python-required-kwarg-checks
--python-check-typeddict-fields --python-check-any-arg-attrs`).

| Backend          | Wall (sum) | Wall (max) | Wall (median) | RSS (max) | RSS (sum) |
|------------------|-----------:|-----------:|--------------:|----------:|----------:|
| default          |     215 s  |    34.2 s  |        2.7 s  |   1.8 GB  |   9.1 GB  |
| `--smt2 --cvc5`  |     338 s  |    64.5 s  |        3.0 s  |   4.3 GB  |  14.4 GB  |

cvc5 is ~57 % slower in total wall time and uses ~58 % more memory.
Both differences are concentrated in 4-5 outliers; the medians are
within 11 %.

## Top time/memory consumers

### default backend (top 5 by wall time)

| Benchmark                       | Wall   | RSS     |
|---------------------------------|-------:|--------:|
| aws_untagged_resources_analyzer | 34.2 s | 1.77 GB |
| test_bedrock_guardrails         | 20.0 s | 1.15 GB |
| sagemaker_labeling_job          | 10.9 s | 0.50 GB |
| cloudwatch_metrics_example      |  9.8 s | 0.35 GB |
| mediaconvert_manager            |  8.1 s | 0.11 GB |

### cvc5 backend (top 5 by wall time)

| Benchmark                       | Wall   | RSS     |
|---------------------------------|-------:|--------:|
| s3_backup_restore               | 64.5 s | 4.27 GB |
| test_bedrock_guardrails         | 51.4 s | 1.48 GB |
| aws_untagged_resources_analyzer | 34.5 s | 1.77 GB |
| websocket_url_validator         | 15.1 s | 1.22 GB |
| sagemaker_labeling_job          | 14.6 s | 0.50 GB |

## Hot-path findings

### `irept::compare` (ecs_utils on cvc5)

`perf` showed 51 % of `ecs_utils.py`'s cvc5 wall time concentrated
in two functions:
* `irept::compare`             — 29 %
* `irept::number_of_non_comments` — 22 %

Call stack: `smt2_convt::set_to` → `convert_typecast` →
recursive `flatten2bv` → `convert_member` / `convert_index` →
`convert_struct`. Each level traverses `defined_expressions`
(`std::map<exprt, irep_idt>`), and `std::map`'s internal
red-black-tree comparison repeatedly re-walks structurally-shared
sub-expressions.

`irept::operator==` already has a SHARING fast path
(`if(data == other.data) return true`). `irept::compare` did not.

**Fix**: add the same fast path to `irept::compare` (commit
4002749dca). Result for `ecs_utils.py` under
`--smt2 --cvc5 --object-bits 12`:

```
Before: 31.6 s wall,  2.4 GB peak RSS
After :  3.4 s wall,   82 MB peak RSS    (10x faster, 30x less memory)
```

The suite-level effect is smaller (~1 % wall reduction on cvc5
overall) because the heavy outliers are solver-bound rather than
compare-bound. But the patch is essentially free — a 4-line
`#ifdef SHARING` short-circuit — with no soundness or correctness
risk, so it lands as a clean win.

### `irept::operator==` (aws_untagged on default)

Top hot symbol is still `irept::operator==` at ~11 % even after the
compare fix. Call stack: `goto_symext::symex_step` →
`goto_symext::symex_assign` → `symex_assignt::assign_rec` →
`field_sensitivityt::field_assignments_rec` → `merge_irept::merged`
→ `irept::operator==`.

`merge_irept::merged` is the symex-level deduplication of irepts.
On structurally-distinct-but-equivalent inputs (which Python's
heterogeneous-dict lowering produces a lot of), the
unordered_set's collision-chain comparisons go through the recursive
operator==.

**Mitigation candidates** (deferred — none free):

1. Pre-compute and cache `number_of_non_comments` on the irept
   itself. `irept` already caches `hash_code`; adding a small
   counter would let `compare` and `operator==` exit early on
   size mismatch in O(1). Trade-off: irept memory grows.
2. Switch `defined_expressions` and `datatype_map` from
   `std::map` to `std::unordered_map` keyed on `irep_hash`. CBMC
   already provides `irep_hash`; the value is cached on the
   irept. Risk: changes iteration order, which CBMC's SMT
   output depends on for determinism.
3. Investigate `merge_irept`'s working set to see if pre-merging
   common Python-front-end-emitted irepts (e.g. the standard
   tagged-union shapes) at front-end startup would reduce the
   set size.

### Solver time (s3_backup_restore, test_bedrock_guardrails on cvc5)

The two cvc5 outliers (52 s, 64 s) are dominated by cvc5 solving
itself, not by CBMC's pre-solver passes. Their profiles show
cvc5-internal stack frames as the hot spots. Reducing this
requires either:
* simpler formulas at the front-end level (fewer string-refinement
  axioms, smaller list/dict bounds for stub-heavy methods); or
* solver-side tuning (cvc5 hint flags, theory-specific options).

These are CBMC-formula-shape and cvc5-solver-tuning concerns rather
than CBMC-frontend code-path concerns. Out of scope for the current
performance round.

## Reproducing

```bash
# Build a debug-info CBMC for source-level profiling
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWITH_JBMC=OFF
cmake --build build-debug --target cbmc -j$(nproc)

# Lower perf paranoid level (root)
sudo sysctl kernel.perf_event_paranoid=-1

# Profile a specific benchmark
export PYTHONPATH=$HOME/python-verification-benchmarks/stubs-full-python
ulimit -v unlimited
perf record -g --call-graph dwarf,8192 -F 997 -e cycles:u \
    -o /tmp/bench.perf.data \
    -- build-debug/bin/cbmc \
    $HOME/python-verification-benchmarks/python-sources/<bench>.py \
    --object-bits 12 [...flag suite...]
perf report -i /tmp/bench.perf.data --stdio --percent-limit 0.5 \
    --no-children -g none | head -25
```

## Status

* **Landed**: `irept::compare` SHARING fast path (4002749dca),
  cuts `ecs_utils` wall by 10x. Suite-level effect ~1 % on cvc5.
* **Identified, deferred**: `irept::operator==` cost in
  `merge_irept::merged`, cvc5 solver-bound outliers. Candidate
  mitigations recorded above.
