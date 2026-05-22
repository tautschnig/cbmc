## CBMC against ESBMC's Python regression suite

A snapshot of how CBMC (this branch's Python frontend at
`cbmc-6.9.0-547-g3c2a693177-dirty`) behaves on ESBMC's Python regression
test suite (`esbmc/regression/python/` at upstream HEAD `3368fcef8`,
3091 tests).

### Methodology

ESBMC's regression descriptors (`test.desc`) are mostly written for
ESBMC's `--incremental-bmc` driver and use ESBMC-specific flags
(`--multi-property`, `--bitwuzla`, `--ir`, `--smt-during-symex`, etc.)
that CBMC does not understand. Rather than translate flag-by-flag we

* ignore the flag line of each `test.desc`,
* run CBMC with `--unwind 10 --no-unwinding-assertions` against the
  test's `main.py`,
* match CBMC's stdout against the expected-output regex on line 4 of
  `test.desc`.

The `test.desc` line-4 regex is almost always `^VERIFICATION
SUCCESSFUL$` or `^VERIFICATION FAILED$`, so this captures
verdict-level agreement reasonably well. A handful of tests assert on
specific Python-level diagnostic strings; those become FAIL if the
wording differs.

The runner is at
`scripts/cbmc_on_esbmc_python.py` (mirrored from
`/tmp/cbmc-runner/run_cbmc_on_esbmc_python.py`); see its `--help` for
options. Per-test timeout 30 s, 16 workers; the suite finishes in
about 80 s wall clock.

### Outcome categories

| Outcome | Count | %     | Meaning                                                                                            |
|---------|------:|------:|----------------------------------------------------------------------------------------------------|
| PASS    | 2118  | 68.5% | CBMC stdout matches the expected regex                                                             |
| DIFF    |  590  | 19.1% | Both ran to verdict but disagree (482 CBMC-FAIL-vs-ESBMC-OK, 108 the reverse)                      |
| UNKNOWN |  215  |  7.0% | `test.desc` has no expected-output regex; CBMC produced a verdict but it cannot be validated      |
| FAIL    |   68  |  2.2% | CBMC produced a verdict but a more specific expected regex did not match (mostly error-text tests) |
| TOERR   |   53  |  1.7% | CBMC ran but produced no verdict (frontend / solver error)                                         |
| CRASH   |   34  |  1.1% | CBMC died with an invariant violation (signal 6)                                                   |
| TIMEOUT |   12  |  0.4% | exceeded 30 s                                                                                      |
| SKIP    |    1  |  0.0% | `try-fail/` has no `main.py`                                                                       |

By tag: CORE 67.9%, THOROUGH 76.1%, KNOWNBUG 62.5%.

Raw per-test results are in `doc/architectural/cbmc-on-esbmc-data/results.csv`.

### CRASHes — 34 invariant violations (highest priority)

| n  | Invariant                                                              | Notes                                                       |
|---:|------------------------------------------------------------------------|-------------------------------------------------------------|
| 22 | `string_expr.h:160` (string-expression precondition)                   | Triggered by `dict32`, `dict33_fail`, several `github_*`    |
|  4 | `satcheck_minisat2.cpp:150` ("variable not added yet")                 | `string-nondet-in-*`, `nondet_dict2`                        |
|  2 | `arith_tools.cpp:149` (`from_integer` precondition)                    | `list`, `list-sort10`                                       |
|  2 | `namespace.h:49` (name not in namespace)                               | `github_3658_4`, `github_3841_4`                            |
|  2 | `boolbv_map.cpp:91` (variable bounds out of range)                     | `github_3033_string-in-price-decimals_fail`, `github_3130_fail` |
|  1 | other                                                                  | `github_3560_4` (variant of above)                          |
|  1 | spurious crash bucket — actually a 15s timeout in re-run                | `list-sort10`                                               |

The full list of triggering tests is in
`doc/architectural/cbmc-on-esbmc-data/buckets.txt`.

The `string_expr.h:160` cluster is the dominant one and most are short
single-file repros (e.g. `dict32`):

```
d = {"a": False}
x = d.get("a")
assert x is False
```

→ Invariant: `from_integer` precondition in `arith_tools.cpp:149` (in
this case `from_integer` is being called from inside the string solver
when the `dict.get` lowering tries to materialise a string slice over
a boolean).

### TOERRs — 53 frontend / solver errors

| n  | Root cause                                                             |
|---:|------------------------------------------------------------------------|
| 41 | `dec_solve: current index set is empty, this should not happen` — string-refinement solver gives up |
|  5 | `stod` parser exception — complex-number string parsing in the Python frontend |
|  7 | other frontend errors (bad return-type inference, `__python_dict__` lookups, etc.) |

Triggering tests: `dict18`, `dict_del*_fail`, `fstring`, `fstring2`,
`esbmc-module-nondet3`, `complex_*`, etc.

### DIFFs — verdict mismatches

590 cases where both tools ran to a verdict but disagree:

* **482**: ESBMC SUCCESSFUL → CBMC FAILED. Mostly methodology, not
  CBMC bugs:
  * 463/482 are pure verdict mismatches with no extra text in CBMC's
    output, dominated by `Python assertion: FAILURE` and
    `uncaught exception: FAILURE`. CBMC at fixed `--unwind 10` flags
    spurious assertion failures on programs that ESBMC's
    `--incremental-bmc` would prove. Bumping `--unwind` and using
    `--depth N` would close some of the gap, but cannot match
    ESBMC's incremental loop semantics directly.
  * 19/482 are "no body for callee f" — undefined functions that
    ESBMC silently stubs and CBMC errors on.

* **108**: ESBMC FAILED → CBMC SUCCESSFUL. **Soundness gaps in
  CBMC's Python frontend or model.** Clusters:
  * complex-number arithmetic edge cases (e.g.
    `complex_pow_zero_neg_fail`: `complex(0,0) ** (-2)` should raise
    `ZeroDivisionError`).
  * `math.comb` with negative argument (`combo1_fail`).
  * `math.floor/ceil(nan)` (`floor_ceil_nan_fail`).
  * dict identity / equality semantics (`dict45_fail`, `class-attributes_fail`).
  * Random-keyed dict deletion (`dict_del12_fail`, `dict_del16_fail`).
  * Typed-dict wrong-type assignment
    (`dict_subscript_typed_assign_fail`).
  * `enumerate` with `start` argument (`enumerate7_fail`).

### Reproducing

```bash
# from cbmc-python.git checkout
cmake --build build --target cbmc -j$(nproc)
python3 scripts/cbmc_on_esbmc_python.py \
    --regression /home/ubuntu/esbmc.git/regression/python \
    --tag all --jobs 16 --timeout 30 --unwind 10 \
    --out doc/architectural/cbmc-on-esbmc-data/results.csv
```

### Triage / fix order

The crashes are the clearest CBMC bugs (they are not user errors;
CBMC's own invariants fire). Suggested order:

1. `string_expr.h:160` (22 tests) — biggest single bucket; reduce a
   minimal repro from `dict32`.
2. `dec_solve: current index set is empty` (41 tests) — likely
   related, in the same string solver.
3. `satcheck_minisat2.cpp:150 'variable not added yet'` (4 tests).
4. The smaller crash clusters (`arith_tools.cpp:149`, `namespace.h:49`,
   `boolbv_map.cpp:91`) — 2 tests each.
5. `stod` complex-number parser (5 tests).
6. The 108 soundness DIFFs as separate, narrowly-scoped fixes.

### Progress (after the first wave of fixes)

The five commits that follow this baseline doc on the
`cbmc-on-esbmc-python` branch land the following:

| Outcome | Baseline | After fixes | Δ      |
|---------|---------:|------------:|-------:|
| PASS    | 2118     | 2137        | +19    |
| DIFF    | 590      | 594         |  +4    |
| UNKNOWN | 215      | 219         |  +4    |
| FAIL    | 68       | 68          |  +0    |
| TOERR   | 53       | 48          |  −5    |
| CRASH   | 34       | 10          | **−24**|
| TIMEOUT | 12       | 14          |  +2    |
| SKIP    | 1        | 1           |  +0    |

Pass rate 68.5 % → 69.1 %.

The 24 fixed crashes split into:
* 22× `string_expr.h:160` — guarded six `simplify_string_*` entry
  points with `can_cast_expr<refined_string_exprt>` so that the
  Python frontend's single-character expressions no longer trip
  the `to_string_expr` precondition.
* 2× `arith_tools.cpp:149 from_integer` — `dict.get` no longer
  builds the int "None" sentinel when the value type can't hold
  it (bool/string/float dicts get `safe_zero`).

The 5 fixed TOERRs are the `complex_*` "stod" frontend aborts:
unparseable complex-number coefficients no longer throw
`std::invalid_argument` out of `convert_constant`.

Soundness fixes (DIFFs that flipped from
ESBMC-FAILED-vs-CBMC-SUCCESSFUL to a real verdict):
* `enumerate(seq, start)` honours the start offset.
* `math.comb`/`factorial`/`perm`/`isqrt` raise ValueError on
  negative arguments.
* `math.floor`/`math.ceil` raise on NaN / ±inf inputs.
* `complex(0+0j) ** <negative>` raises ZeroDivisionError.

The 10 remaining CRASHes (4× satcheck_minisat2.cpp:150,
3× boolbv_map.cpp:91, 2× namespace.h:49, 1× misc) are deeper
solver-layer issues — symptoms include stale-pointer
`symbol_table1->symbols.size()` returning garbage during
`namespacet::lookup`. They need a separate, broader fix.
