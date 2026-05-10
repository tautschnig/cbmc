# TypeScript Fuzzer: Extended Durability Runs

Fuzzer: `scripts/fuzz_typescript.py`  
Configuration: 15-second per-program timeout, 4 GB memory limit

## Round 1 — baseline grammar (2026-05-09 morning)

Two back-to-back runs with different seeds:

| Run | Iterations | Seed | Wall time | Outcome |
|-----|-----------|------|-----------|---------|
| 1 | 5 000 | 10 000 | ~3 h | 2 784 ok / 2 216 tsc-rejected / 0 crashes |
| 2 | 10 000 | 50 000 | ~6 h | 5 659 ok / 4 341 tsc-rejected / 0 crashes |
| **Total** | **15 000** | | **~9 h** | **8 443 ok / 0 crashes** |

All per-program outcome counters stayed at zero throughout.

## Round 2 — extended grammar (2026-05-09 evening)

Extended the grammar with: destructuring, optional chaining +
nullish coalescing, Promise chains, generic calls with explicit
type arguments, throw/catch with actual throw, switch statements,
template literals with interpolations, arrow functions, class
inheritance. The first 100-iteration shake-out found two frontend
bugs which were fixed (optional-field init lost the value;
template-literal interpolation of unary minus / binary arithmetic
fell back to nondet). Full run:

| Run | Iterations | Seed | Wall time | Outcome |
|-----|-----------|------|-----------|---------|
| 3 | 5 000 | 200 000 | ~3 h 15 min | 3 335 ok / 1 664 tsc-rejected / 0 real crashes |

The one "UNKNOWN(126)" (exit code) was a transient artifact:
`bash: /home/ubuntu/cbmc-python.git/build/bin/cbmc: Permission
denied` — the fuzz loop ran a CBMC invocation at the exact moment
we were linking a new CBMC binary from other fixes. Not a fuzzer
finding; the elapsed time (0.0017 s) and stderr confirm this.

## Bugs found by round 2

Two genuine frontend bugs were uncovered during the initial 100-
iteration shake-out of the extended grammar:

1. **Optional-field init lost the value** — `{ x?: number } = { x: 3.14 }`
   kept the trailing `?` in the struct component's name, so
   object-literal field matching defaulted to NaN.
2. **Template-literal interpolation** of unary minus or binary
   arithmetic (e.g. `` `v=${42 * -1}` ``) fell back to nondet,
   giving an unconstrained string length.

Both fixed with CORE regression tests (`optional-field-init`,
`template-literal-arithmetic`). After the fixes, the 5 000-
iteration main run was clean.

## Cumulative coverage

| Round | Programs verified | Crashes |
|-------|-------------------|---------|
| 1 (baseline) | 8 443 | 0 |
| 2 (extended) | 3 335 | 0 |
| **Total** | **11 778** | **0** |

## Recommendations

1. **Current grammar looks robust.** 11 778 verified programs, 0
   crashes. The two bugs round 2 found were in features the
   extended grammar covered for the first time — a clear win for
   grammar extension.

2. **Further grammar extensions** to consider for a future round:
   - Decorators
   - Parameter property declarations (`constructor(public x: number) {}`)
   - Complex generic bounds (`<T extends Comparable<T>>`)
   - Index signature types (`{ [key: string]: number }`)
   - Mapped / conditional types
   - Tuple-type manipulation (`type R = [...T, U]`)

3. **Seed-with-real-TS strategy:** mutation-based fuzzing starting
   from real TypeScript codebases would explore different parts
   of the AST than our generative grammar can. Higher setup cost
   but much better coverage of idiomatic patterns.

4. **Property-based fuzzing** remains attractive: generate programs
   that should satisfy specific invariants (e.g.
   `JSON.parse(JSON.stringify(x)) === x`) and verify them. The
   current generator produces tautologies; property-based tests
   would catch more semantic bugs.

## CI integration

The per-PR CI volume (100 iterations, seed 42) caught both bugs
found in round 2 within minutes — before a human reviewed the
extended-grammar PR. The 11 778-iteration local durability run
gives confidence that the frontend is robust for the grammar's
current scope.
