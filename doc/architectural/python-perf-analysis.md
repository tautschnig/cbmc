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

## Layered call-stack breakdown — what dominates each backend

The "irept hot symbols" view in the previous section is the
*self-time* picture: which functions are actually executing
the most cycles. By itself this can be misleading — for many
benchmarks the answer "where do those cycles come from?" is
more useful, because the fix often lives at a different
layer than the leaf hot symbol.

The table below aggregates `perf report --children` output
for each benchmark, grouping per-symbol cycles into
call-stack-level categories. *Children %* is the children-time
of the highest-level enclosing function in each category
(what fraction of the run is spent inside this layer or
below). *Self %* is the self-time of all symbols in the
category (cycles literally executed in those functions).

Source data: `python-perf-snapshots/<bench>.report-children.txt`
and `<bench>.report-callgraph.txt`.

### `aws_untagged_resources_analyzer.py` — default backend, 34.2 s wall

96.6 % cbmc / 2.7 % python3 / 0 % cvc5. Heaviest CBMC-bound
benchmark in the suite.

| Category                                            | Children % | Self % |
|----------------------------------------------------:|-----------:|-------:|
| `goto_symext::symex_step` and below                 |     71.0   |   0.0  |
| &nbsp;&nbsp;`symex_assign` chain (descendants)      |     49.2   |   0.0  |
| &nbsp;&nbsp;`field_sensitivity` recursive expansion |     27.3   |   0.3  |
| &nbsp;&nbsp;`merge_gotos` / `phi_function` (branch-join SSA) | 22.0 | 0.2 |
| &nbsp;&nbsp;`dereference` / `clean_expr`            |     17.9   |   0.0  |
| `merge_ireps` (irept dedup)                         |     21.4   |   5.5  |
| `build_identifier` / SSA renaming                   |     17.6   |   0.5  |
| `irept` primitives (`operator==`, `compare`, `find`, `get`) | 11.1 | **24.7** |
| stdlib (`malloc` / `free` / `operator new`)         |     13.5   |   8.0  |

**Why we invoke these.** The benchmark has 953 GOTO
instructions across its Python functions, with the longest
single function (`run_analysis`) at 512. Inside that we have
nested for-loops over dicts, list comprehensions, dict
comprehensions, and try/except handlers — every single one
is a Python-frontend-emitted cascade of `python_value`-typed
struct assignments. CBMC then expands each struct assignment
field-by-field via `field_sensitivityt::field_assignments_rec`
(5 levels of recursion in the trace) and merges the resulting
SSA at every branch join via `merge_gotos`. The 24.7 %
self-time in irept primitives is the *symptom*; the 49 %
spent in `symex_assign` is the *reason* we call them.

### `test_bedrock_guardrails.py` — cvc5 backend, 51.4 s wall

42.8 % cvc5 / ~50 % cbmc / 2.4 % python3.

| Category                                            | Children % | Self % |
|----------------------------------------------------:|-----------:|-------:|
| `cvc5` solver internals (stripped binary, no symbols) |   —      |  42.8  |
| `goto_symext::symex_step` and below                 |     14.6   |   0.0  |
| &nbsp;&nbsp;`symex_assign` chain                    |     20.0   |   0.0  |
| &nbsp;&nbsp;`field_sensitivity` recursive expansion |     18.4   |   0.1  |
| `merge_ireps` (irept dedup)                         |      5.7   |   0.8  |
| `irept` primitives (self)                           |      6.6   |  11.2  |
| stdlib (self)                                       |      7.0   |   4.1  |
| Python frontend (parse/convert)                     |      0.7   |   0.0  |

**Why we invoke these.** Heavy boto3-stub interaction. Every
`bedrock.create_guardrail(name=…, description=…,
topicPolicyConfig=…)` call goes through a stub method body
that asserts the kwargs against a TypedDict schema and
returns a non-deterministic struct. Each return path emits a
struct equality, which under cvc5 becomes a string-refinement
axiom. Roughly half of wall time is cvc5 working through
those axioms; the cbmc-side ~20 % is the same `symex_assign` /
`field_sensitivity` cascade as `aws_untagged`, scaled smaller
because the user code is shorter.

### `s3_backup_restore.py` — cvc5 backend, 64.5 s wall

86.5 % cvc5 / 1.9 % cbmc / 1.6 % python3. The most
solver-bound outlier.

| Category | Children % | Self % |
|---|---:|---:|
| `cvc5` solver internals | — | 86.5 |
| Anything in cbmc | < 1 | < 1 |

**Why we invoke cvc5 so much.** The CBMC side finishes its
job in seconds — it produces a 3-4 MB SMT-LIB file with
extensive string-refinement axioms over kwarg-key sets across
many boto3 S3 methods. cvc5 then takes ~62 s and 4.3 GB of
memory churning through the refinement loop. Effectively
zero of this time is in CBMC's own code; reducing it requires
either smaller formulas (CBMC frontend / refinement-loop
output) or solver tuning.

### `ecs_utils.py` — cvc5 backend, 3.4 s wall (after the SHARING fast-path)

48.9 % cbmc / 36.9 % python3 / 15.0 % cvc5.

| Category                              | Children % | Self % |
|--------------------------------------:|-----------:|-------:|
| Python AST parsing of stubs (libpython) |   35.7   |  27.3  |
| `cbmc_parse_optionst::doit` and below |     19.8   |   0.0  |
| &nbsp;&nbsp;`goto_symext::symex_step` |     14.5   |   0.0  |
| &nbsp;&nbsp;&nbsp;&nbsp;`symex_assign` chain |  0.8 |   0.0  |
| `cvc5` solver internals               |       —    |  15.0  |

**Why this profile is dominated by Python parsing.** After
the `irept::compare` SHARING fast-path landed, this benchmark
went from 32 s to 3.4 s. The *rest* of the pipeline now
matters more in relative terms. CBMC shells out to `python3
-m ast` once per imported module to parse `.py` source, and
`boto3/__init__.py` alone is ~5,000 lines (because of the
per-service overload chain). In this short benchmark, parsing
the stubs takes longer than the verification itself.

This is the headline finding for short benchmarks across the
suite: stub-parse time is now a non-trivial fixed cost. See
`python-parse-daemon-design.md` for the proposed mitigation.

## `--slice-formula` measurement

Hypothesis: slicing the SMT formula to retain only the
transitive support of each property would mainly help the
solver-bound benchmarks. Verified empirically — the win is
larger than expected on cvc5 outliers but caveated by a
soundness gap on string-format intrinsics.

| | wall (sum) | wall (max) | RSS (sum) | RSS (max) |
|---|---:|---:|---:|---:|
| default, `--slice-formula` off | 215 s | 34.2 s |  9.1 GB | 1.7 GB |
| default, `--slice-formula` on  | 211 s | 34.8 s |  8.3 GB | 1.7 GB |
| cvc5,    `--slice-formula` off | 338 s | 64.5 s | 14.4 GB | 4.3 GB |
| cvc5,    `--slice-formula` on  | 223 s | 34.8 s |  8.5 GB | 1.7 GB |

The cvc5 backend gains 34 % wall time, 41 % RSS-sum, and 60 %
RSS-max. The default backend gains ~2 % wall time and ~9 %
RSS-sum (modest — the boolbv lowering is less sensitive to
unused assertions). Pass rate is unchanged at 94.1 % on both
backends.

### Per-benchmark cvc5 winners

| Benchmark | Wall before | Wall after | Speedup | RSS before | RSS after |
|---|---:|---:|---:|---:|---:|
| `s3_backup_restore`              | 64.5 s |  3.2 s | **20.2×** | 4271 MB | **60 MB** |
| `clear_duplicate_dynamodb_entries` |  9.5 s |  1.6 s |  6.0× | 1146 MB |  36 MB |
| `test_bedrock_guardrails`        | 51.4 s | 17.9 s |  2.9× | 1479 MB | 928 MB |
| `sagemaker_labeling_job`         | 14.6 s | 10.7 s |  1.4× |  495 MB | 495 MB |
| `cloudwatch_metrics_example`     | 11.6 s |  9.2 s |  1.3× |  355 MB | 313 MB |

`s3_backup_restore` is the standout: the cvc5 OOM that
required raising `ulimit` to 8 GB now runs in 60 MB.

### Initial soundness blocker (fixed in 3c2a693177)

When first enabled, four regression tests failed with
`--slice-formula`:

* `regression/python/str-format-int-precision`
* `regression/python/fstring-int-precision`
* `regression/python/fstring-multi-arg`
* `regression/python/fstring-pad-spec`

Investigation traced the issue to a slicer ↔ string-
refinement interaction. The CBMC string-refinement decision
procedure consumes side-channel information from three
families of intrinsic calls:

  * `cprover_associate_array_to_pointer_func(arr, ptr)` —
    populates `array_pool::arrays_of_pointers[ptr] = arr`.
  * `cprover_associate_length_to_array_func(arr, len)` —
    adds the constraint `arr.length == len`.
  * `cprover_string_*_func(...)` — the actual string
    builtins that `string_constraint_generator` axiomatises.

These calls are emitted by the Python front-end as SSA
assignments of the shape

  __assoc_rc = cprover_associate_array_to_pointer_func(arr, ptr)

— a function-call whose return-code LHS is an int symbol
that nothing else uses. The slicer's data-flow algorithm
correctly noted "LHS not transitively used by any
assertion" and dropped the assignment, removing the *call*
along with the side channel it carried. Without
`associate_array_to_pointer`, the refinement engine's
`add_axioms_for_length` for
`cprover_string_length_func({2, ptr})` falls through to a
fresh nondet length symbol instead of returning the known
constant 2.

The `--show-vcc` output makes the difference visible. With
`--slice-formula`:

```
{-1} python::__string_len_7#0 = 0
{-2} python::__str_int_14#1 =
       cprover_string_length_func({ 2, address_of(42_constant_char_array[0]) })
```

Without, the same VCC includes the registration call:

```
…
{-9} python::__string_ptr_7#1 = address_of(42_constant_char_array[0])
{-10} return_value!0#1 =
        cprover_associate_array_to_pointer_func({ 52, 50 },
                                                address_of(42_constant_char_array[0]))
{-11} goto_symex::return_value::python::stringify!0#1..length = 2
…
```

The user's original framing — *"It is possible, though
unlikely, that the code implementing --slice-formula has
a bug. More likely is that this points to a bug elsewhere"*
— turned out to be **half right**: the soundness gap was in
the slicer, but caused by an interface contract the slicer
didn't honour rather than a bug in its core data-flow logic.
The fix is to teach the slicer that any assignment whose
RHS contains a `function_application_exprt` to a
`cprover_string_*` / `cprover_char_*` / `cprover_associate_*`
symbol must be preserved regardless of LHS reachability.

Implementation: ~30 lines in
`src/goto-symex/slice.cpp::contains_string_refinement_intrinsic`
(a depth-first scan for the relevant function names) plus a
4-line guard at the top of `slice_assignment`. Four locked-in
regression tests at
`regression/python/<bench>/slice-formula.desc` exercise the
existing test bodies with `--slice-formula` explicitly on.

### Recommendation

`--slice-formula` is now default-on for `.py` source (commit
3716b7ce3c) — the soundness blocker is gone, the pass rate
is unchanged at 94.1 %, and the cvc5 wall savings are
substantial. Pass `--no-slice-formula` to disable.

## Combined: parse daemon + `--slice-formula`

The Python parse daemon
(`doc/architectural/python-parse-daemon-design.md`) and
`--slice-formula` are independent — daemon attacks the
fork/exec-and-Python-startup cost, slice attacks the SMT
solver cost. They compose cleanly.

Suite-wide on `--smt2 --cvc5 --object-bits 12` (8 GB ulimit,
51 benchmarks):

| Configuration                     | Wall (sum) | Wall (median) | Wall (max) | RSS (max) |
|----------------------------------:|-----------:|--------------:|-----------:|----------:|
| baseline cvc5                     |     338 s  |        3.0 s  |    64.5 s  |   4.3 GB  |
| + parse daemon                    |     273 s  |        1.7 s  |    62.9 s  |   4.2 GB  |
| + `--slice-formula`               |     223 s  |        2.8 s  |    34.8 s  |   1.7 GB  |
| **+ daemon + `--slice-formula`**  | **157 s**  |    **1.5 s**  | **32.4 s** |  1.7 GB   |

For reference, the **default backend** (boolbv + MiniSat) on
the same 51-benchmark suite takes 215 s. With both
optimisations on, **cvc5 is now slightly faster than the
default backend** suite-wide, while preserving the same
94.1 % pass-rate.

`--slice-formula` is default-on for `.py` source as of
commit 3716b7ce3c. The parse daemon is opt-in via the
`CBMC_PYTHON_SERVER_SOCKET` env var.

## Updated layered breakdown (post-optimisation)

After the daemon + `--slice-formula` defaults landed, the
same four representative benchmarks were re-profiled. The
shape of the work has shifted significantly. Source data:
`python-perf-snapshots/<bench>-final.report-{self,children,callgraph}.txt`.

### Suite-level wall and memory (51 benchmarks)

| Configuration                       | Wall (sum) | Median | Max | RSS (sum) | RSS (max) |
|------------------------------------:|-----------:|-------:|----:|----------:|----------:|
| **default**, baseline               |    215 s   |  2.7 s | 34.2 s |   9.1 GB |   1.7 GB |
| **default**, slice on (default-on)  |    210 s   |  2.7 s | 35.0 s |   8.3 GB |   1.7 GB |
| **default**, slice on + daemon      |  **152 s** |**1.6 s**|34.5 s|   8.3 GB |   1.7 GB |
| **cvc5**, baseline                  |    338 s   |  3.0 s | 64.5 s |  14.4 GB |   4.3 GB |
| **cvc5**, slice on (default-on)     |    223 s   |  2.8 s | 35.0 s |   8.5 GB |   1.7 GB |
| **cvc5**, slice on + daemon         |  **162 s** |**1.6 s**|34.0 s|   8.6 GB |   1.7 GB |

The two backends now run in nearly the same time and
memory; cvc5 is just **6 %** slower than default at the
suite level (162 s vs 152 s). Pass rate unchanged at
94.1 % on both.

### Per-outlier trajectory

| Benchmark / backend                       | baseline       | + slice         | + slice + daemon |
|-------------------------------------------|----------------|-----------------|------------------|
| `aws_untagged_resources_analyzer` default | 35.0 s / 1.77 GB | 35.0 s / 1.77 GB | 34.5 s / 1.77 GB |
| `aws_untagged_resources_analyzer` cvc5    | 34.9 s / 1.77 GB | 35.0 s / 1.77 GB | 34.0 s / 1.77 GB |
| `test_bedrock_guardrails` default         | 20.4 s / 1.15 GB | 17.6 s / 0.93 GB | 16.5 s / 0.93 GB |
| `test_bedrock_guardrails` cvc5            | 52.6 s / 1.48 GB | 17.8 s / 0.93 GB | 16.8 s / 0.93 GB |
| `s3_backup_restore` default               |  2.7 s / 90 MB   |  2.7 s / 66 MB   |  1.6 s / 66 MB   |
| `s3_backup_restore` cvc5                  | 29.7 s / 3.82 GB |  3.2 s / 59 MB   |  2.4 s / 60 MB   |
| `ecs_utils` default                       |  3.9 s / 0.41 GB |  3.0 s / 176 MB  |  1.9 s / 175 MB  |
| `ecs_utils` cvc5                          | 31.8 s / 2.40 GB |  2.8 s / 72 MB   |  1.8 s / 72 MB   |

Notes on each:

* `aws_untagged_resources_analyzer` — the suite's heaviest
  symex-bound benchmark. Neither slice (it's already
  unsliced — most assignments matter for the assertion
  closure) nor daemon (it's a single-file run with few
  imports) helps materially. Default and cvc5 backends
  match because the formula is also short.
* `test_bedrock_guardrails` — slice cuts the cvc5 time by
  3× by removing string-refinement axiom volume cvc5
  doesn't need. Default backend gets ~14 % from the same
  effect.
* `s3_backup_restore` — the famous OOM. Slice alone cuts
  cvc5 time 9× and memory 65×; daemon's effect is smaller
  here.
* `ecs_utils` — already mostly fixed by the
  `irept::compare` SHARING fast-path. With slice + daemon
  it's now faster than the default backend was a month ago.

### What changed in the layered breakdown

For each of the four benchmarks below, *children* % is the
fraction of cbmc-attributed wall time spent in that layer
or below; *self* % is the actual cycles executing in the
listed functions. comm split is `cbmc / cvc5 / python3`.

#### `aws_untagged_resources_analyzer` (no major change)

Both backends: cbmc 99.8 %, cvc5 0.0 %, python3 0.0 %.
Identical to baseline within noise — this benchmark is
symex-bound and our optimisations target the solver and
parser.

| Layer                                    | children % | self % |
|-----------------------------------------:|-----------:|-------:|
| `goto_symext::symex_step` and below      |     72.1   |   0.0  |
| &nbsp;&nbsp;`symex_assign` chain         |     49.8   |   0.0  |
| &nbsp;&nbsp;`field_sensitivity` recursive |    27.4   |   0.4  |
| &nbsp;&nbsp;`merge_gotos`/`phi`           |    22.3   |   0.2  |
| &nbsp;&nbsp;`dereference`/`clean_expr`    |    18.2   |   0.0  |
| `merge_ireps` (irept dedup)              |     21.4   |   5.6  |
| string-refinement loop                   |     20.9   |   5.5  |
| `build_identifier` / SSA renaming        |     17.9   |   0.6  |
| `irept` primitives (self)                |     11.3   |  25.6  |
| stdlib (self)                            |     13.1   |   7.9  |
| slice-formula                            |      2.7   |   1.1  |

#### `test_bedrock_guardrails` (cvc5: 51 % was solver, now 0.5 %)

| | cbmc | cvc5 | python3 |
|---|---:|---:|---:|
| baseline cvc5  | ~50 % | 42.8 % | 2.4 % |
| **post cvc5**  | **100.3 %** | **0.5 %** | **0 %** |

The 50-line `field_sensitivity` cascade now visibly
dominates instead of being half-hidden by cvc5. cbmc-side
breakdown for both backends is essentially identical post-
fix:

| Layer                                | children % | self % |
|-------------------------------------:|-----------:|-------:|
| `goto_symext::symex_step` and below  |     45.8   |   0.0  |
| &nbsp;&nbsp;`symex_assign` chain     |     63.7   |   0.0  |
| &nbsp;&nbsp;`field_sensitivity`      |     58.8   |   0.4  |
| `merge_ireps`                        |     17.7   |   2.7  |
| string-refinement loop               |     17.3   |   2.6  |
| `irept` primitives (self)            |     13.1   |  30.2  |
| stdlib (self)                        |     14.4   |   7.9  |

#### `s3_backup_restore` (cvc5: 86 % was solver, now 29 %)

| | cbmc | cvc5 | python3 |
|---|---:|---:|---:|
| baseline cvc5 | 1.9 % | 86.5 % | 1.6 % |
| **post default** | 99.9 % | 0.0 % | 0 % |
| **post cvc5**    | 71.0 % | 29.2 % | 0 % |

The default backend now solves this benchmark in 1.6 s with
sliced formula (vs 2.7 s before — formula slicing also
helps boolbv slightly). Post-optimisation cvc5 still has a
sizeable solver share (29 %) but the absolute time is just
2.4 s.

| Layer                                  | children % | self % |
|---------------------------------------:|-----------:|-------:|
| `symex_step` and below                 |     13.7   |   0.0  |
| &nbsp;&nbsp;`field_sensitivity`        |     22.0   |   0.2  |
| &nbsp;&nbsp;`dereference`/`clean_expr` |     14.4   |   0.0  |
| `build_identifier` / SSA renaming      |     11.8   |   0.4  |
| stdlib (self)                          |     12.9   |   8.2  |
| `irept` primitives (self)              |      6.0   |   8.7  |
| Python frontend                        |     11.0   |   0.1  |

#### `ecs_utils` (Python parse-time gone, balanced)

| | cbmc | cvc5 | python3 |
|---|---:|---:|---:|
| baseline cvc5 (post irept fix)  | 48.9 % | 15.0 % | **36.9 %** |
| **post default**                | 100.0 % | 0.0 % | **0 %** |
| **post cvc5**                   | 90.2 % | 9.5 % | **0 %** |

The 37 % python3 share is gone (daemon serves all parse
requests in-process). cvc5's residual 9.5 % is from the
small-but-real string-refinement work this benchmark
exercises; default backend has zero solver cost because the
sliced formula is trivial for boolbv + MiniSat.

| Layer                                 | children % | self % |
|--------------------------------------:|-----------:|-------:|
| `symex_step` and below                |     29.6   |   0.0  |
| &nbsp;&nbsp;`symex_assign`            |     14.7   |   0.0  |
| `merge_ireps`                         |      8.1   |   1.6  |
| `build_identifier`/SSA renaming       |      7.0   |   0.1  |
| string-refinement loop                |      8.1   |   1.6  |
| Python frontend                       |     14.9   |   0.0  |
| `irept` primitives (self)             |      6.2   |  14.8  |
| stdlib (self)                         |     14.9   |   7.4  |

## What the layered view tells us

- **The "irept hot symbols" view in isolation was misleading.**
  The actual root cause of long wall times is different per
  benchmark:

  - `aws_untagged` (default): `symex_assign` blowup from
    Python's struct-heavy IR. The 24.7 % self-time in irept
    primitives is a *symptom*; the cause is the 49 % spent
    emitting per-field SSA assignments through
    `field_sensitivityt::field_assignments_rec`.
  - `test_bedrock_guardrails` (cvc5): half-and-half — cvc5
    working on string-refinement axioms (43 %) plus the same
    struct-assignment blowup (~20 %).
  - `s3_backup_restore` (cvc5): solver internals (86.5 %),
    nothing else matters.
  - `ecs_utils` (cvc5, post-fix): Python-stub parsing (37 %)
    is now the largest single component because the rest got
    fast.

- **Optimization opportunities cluster differently.** A
  single fix won't help all four. In rough ROI order:

  1. **Reduce field-by-field SSA expansion on `python_value`
     structs.** Either cap `field_sensitivity` recursion
     depth on `python_value`, or emit struct-level
     (not field-level) SSA for tagged-union assignments.
     Hits the largest CBMC-bound outlier
     (`aws_untagged_resources_analyzer`) and several others.
  2. **Parse-daemon for stub `.py` files.** Cache the
     `python3 -m ast` JSON output across runs, keyed on file
     content / mtime. Helps short benchmarks where the
     ~37 % stub-parse fixed cost dominates.
  3. **Cut SMT axiom volume from
     `--python-required-kwarg-checks`.** The current
     formulation emits O(K²) checks for K kwargs; an O(K)
     hash-based formulation would directly translate to less
     cvc5 work on `test_bedrock_guardrails`-shaped
     benchmarks.
  4. **`--slice-formula`** to drop unused SMT assertions
     before solving. Won't reduce the symex side but may
     shrink cvc5's working set on bedrock / s3 backup. (See
     §`--slice-formula` measurement below.)
  5. **Solver-side tuning** for `s3_backup_restore`-shaped
     benchmarks (cvc5 hint flags, theory-specific options).

- **The 4-5 outliers driving the suite's wall-time gap
  between default and cvc5 are not all the same problem.**
  Saying "cvc5 is slower" is correct on average but obscures
  that `s3_backup_restore` is purely solver-bound while
  `aws_untagged_resources_analyzer` is purely symex-bound and
  `test_bedrock_guardrails` is half each. The optimization
  budget should be spent accordingly.

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
