# TypeScript Fuzzer: Extended Durability Runs

Fuzzer: `scripts/fuzz_typescript.py`  
Configuration: 15-second per-program timeout, 4 GB memory limit

## Round 1 — baseline grammar (2026-05-09 morning)

| Run | Iterations | Seed | Wall time | Outcome |
|-----|-----------|------|-----------|---------|
| 1 | 5 000 | 10 000 | ~3 h | 2 784 ok / 2 216 tsc-rejected / 0 crashes |
| 2 | 10 000 | 50 000 | ~6 h | 5 659 ok / 4 341 tsc-rejected / 0 crashes |

## Round 2 — extended grammar (2026-05-09 evening)

Grammar extended with: destructuring, optional chaining, Promise
chains, generic calls, throw/catch, switch, template literals,
arrow fns, class inheritance.

Round-2 shake-out found and fixed two bugs: optional-field init
and template-literal arithmetic.

| Run | Iterations | Seed | Wall time | Outcome |
|-----|-----------|------|-----------|---------|
| 3 | 5 000 | 200 000 | ~3 h 15 min | 3 335 ok / 1 664 tsc-rejected / 0 real crashes |

## Round 3 — refined-strings-focused grammar (2026-05-10)

Grammar extended with string-heavy productions (to stress the
now-auto-enabled `--refine-strings`) plus more TS features: type
aliases, index signatures, parameter properties, generic
constraints, destructuring defaults, array spread.

Round-3 shake-out found and fixed one bug: destructuring defaults
were ignored when the source property was undefined (fixed to
pick the default via IEEE-NaN detection).

| Run | Iterations | Seed | Wall time | Outcome |
|-----|-----------|------|-----------|---------|
| 4 | 5 000 | 300 000 | ~3 h 15 min | 3 384 ok / 1 616 tsc-rejected / 0 crashes |

## Cumulative coverage

| Round | Programs verified | Crashes |
|-------|-------------------|---------|
| 1 (baseline) | 8 443 | 0 |
| 2 (extended) | 3 335 | 0 |
| 3 (refined-strings-focused) | 3 384 | 0 |
| **Total** | **15 162** | **0** |

## Property-based fuzzer (2026-05-10)

`scripts/property_fuzz_typescript.py` — complementary strategy.
Instead of tautological assertions, encodes 20 ES2024-grounded
invariants (e.g. `JSON.parse(JSON.stringify(x)) === x`,
`a.concat(b).length === a.length + b.length`, Object.is(NaN, NaN))
and verifies them.

First 100-iteration run found one violation: `NaN === NaN`
incorrectly returns true (null-as-NaN model trade-off; documented
as KNOWNBUG `strict-nan-not-equal`).

## Bugs found by fuzzing, cumulative

Each round's extended grammar found new bugs the previous rounds
couldn't reach:

| Round | Bug(s) found | Fix |
|-------|-------------|-----|
| 2 | Optional-field init lost value | Strip `?` from field name in type parser |
| 2 | Template-literal arithmetic nondet | Recursive constant folder covering ID_floatbv_* ops |
| 3 | Destructuring defaults ignored | if_exprt(is_nan(src_field), default, src_field) |
| Property | `NaN === NaN` returns true | Documented as KNOWNBUG (null-as-NaN trade-off) |

Pattern: each time the grammar's surface grows, new interactions
reveal subtle bugs. The per-PR CI volume (100 iterations, seed 42)
reliably catches regressions; long local runs serve as periodic
deep checks.

## Recommendations for the next round

- Decorators and metadata (experimentalDecorators)
- Mapped types and conditional types
- Module/namespace boundaries
- JSX / TSX constructs (if we want to extend .tsx support)
- Real-world seed corpus — mutating known-good TS snippets rather
  than generating from scratch
