# TypeScript Fuzzer: Extended Durability Run

Date: 2026-05-09  
Fuzzer: `scripts/fuzz_typescript.py`  
Configuration: 15-second per-program timeout, 4 GB memory limit

## Run summary

Two back-to-back runs with different seeds to cover different parts
of the generator's search space:

| Run | Iterations | Seed | Wall time | Outcome |
|-----|-----------|------|-----------|---------|
| 1 | 5 000 | 10 000 | ~3 h | 2 784 ok / 2 216 tsc-rejected / 0 crashes |
| 2 | 10 000 | 50 000 | ~6 h | 5 659 ok / 4 341 tsc-rejected / 0 crashes |
| **Total** | **15 000** | | **~9 h** | **8 443 ok / 0 crashes** |

Per-program outcome counters stayed at zero throughout:
- `assertion_failed`: 0 (all generated programs verify — assertions
  are tautologies by construction)
- `segfault`: 0
- `abort`: 0
- `timeout`: 0
- `parse_error`: 0
- `unknown`: 0

## What this means

- The frontend is robust across 8 443 distinct type-checked programs
  covering: literals, arithmetic, string concatenation, comparison,
  booleans, `for`/`while` loops with bounded counters, `if`
  statements, assertion statements, function declarations, class
  declarations, array literals, array method calls (`map`,
  `filter`, `reduce`, `slice`, `concat`, `reverse`, `includes`,
  `indexOf`, `at`), Map and Set operations, `try`/`catch`, and
  `async`/`await` patterns.

- The 4 341 programs rejected by `tsc` are an artifact of the
  grammar (random type mixes that TypeScript's type checker
  correctly rejects as ill-typed). We use `tsc` as an oracle
  precisely to skip these — they're not fuzzer failures.

- Zero crashes after 15 000 iterations suggests the frontend's
  error-handling paths are well-covered for the grammar's scope.
  Further fuzzing at the same grammar is unlikely to find new
  issues without extending the grammar.

## Recommendations

1. **Expand the grammar** — add:
   - Destructuring assignments
   - Optional chaining / nullish coalescing
   - More async patterns (Promise.all, Promise.race, .then chains)
   - JSON.stringify / parse calls with symbolic nested objects
   - Generic function calls with different type-argument shapes
   - Abstract / override classes
   - Switch with multiple case types
   - Exception throwing and handling variants

2. **Run longer once grammar is extended** — 15 000 iterations was
   apparently enough for the current grammar; a broader grammar
   should be exercised for a similar number.

3. **Seed with real TS** — a more advanced strategy would fuzz
   around existing TypeScript programs (insert / delete / mutate
   tokens). This would catch patterns that purely-generative
   fuzzing misses.

4. **Consider property-based fuzzing** — generate programs that
   should satisfy specific invariants (e.g. `JSON.parse(JSON.stringify(x))
   === x`) and verify them. Different from the tautology-based
   current approach.

## CI integration

The fuzzer already runs 100 iterations per PR (seed 42) via
`.github/workflows/typescript-fuzz.yaml`. The durability results
above suggest that the CI volume is sufficient to catch regressions
quickly, while long local runs serve as periodic deep checks.
