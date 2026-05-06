# TypeScript Frontend Performance Report

**Date:** 2026-05-06
**Tests:** 500 CORE, 1 KNOWNBUG
**Total verification time:** ~470s (7.8 minutes)
**Average per test:** 939ms
**Max per test:** 2540ms (verify-gcd-nondet with --ts-integer-mode)

## Slowest Tests (top 15)

| Time (ms) | Test |
|-----------|------|
| 2540 | verify-gcd-nondet |
| 1681 | verify-div-zero |
| 1110 | while-nondet |
| 1018 | verify-nondet-triangle |
| 1018 | verify-map-has |
| 1013 | power-math |
| 1011 | verify-300-milestone |
| 1009 | verify-absolute-diff |
| 1004 | map-iteration |
| 1002 | verify-map-iter-sum |
| 1001 | verify-string-reverse |
| 994 | verify-nondet-array-sum |
| 993 | verify-map-lookup |
| 992 | verify-array-reduce-max |
| 991 | verify-nondet-bounded-sum |

## Observations

### What's fast
- Constant-folding tests (arithmetic, string methods on constants): <500ms
- Simple class tests without nondet: ~300-600ms
- Pure loop unrolling (small unwind counts): <500ms

### What's slower
- **Nondet + modulo loops** (verify-gcd-nondet): 2.5s even with integer mode
  Integer modulo on nondet values creates large SAT formulas
- **Division/NaN checks** (verify-div-zero): 1.7s — IEEE 754 division
  encoding is expensive
- **Map/Set with linear scans** (verify-map-*): ~1s each — scanning 8 slots
- **While loops with nondet** (while-nondet): 1.1s

### Potential optimizations

1. **Reduce Map/Set scan depth** — default is 8 entries, could be 4 if
   tests don't exceed that. Would halve Map/Set test times.

2. **Inline simple getters** — small methods like `getValue()` could be
   inlined to avoid call overhead.

3. **Eager constant folding** — more aggressive folding in expression
   handler could avoid some nondet path explosions.

4. **Cache function conversions** — generic specializations rebuild the
   same code; caching would help for repeated instantiations.

## Scaling Characteristics

- **Linear in test count** — adding tests doesn't affect individual times
- **Quadratic in unwind** — loop-heavy tests scale with unwind²
- **Exponential in nondet breadth** — more nondet variables explode path
  count

## Recommendations

1. For development: use default settings, typical <1s per test
2. For CI: batch tests, total <10 minutes acceptable
3. For profiling individual slow tests: use `--verbosity 9` to identify
   solver bottlenecks
4. For production workloads: consider `--ts-integer-mode` when applicable
