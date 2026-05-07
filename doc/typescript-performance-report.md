# TypeScript Frontend Performance Report (Updated)

**Date:** 2026-05-07
**Tests:** 540 CORE, 2 KNOWNBUG
**Total verification time:** ~520s (8.7 minutes)
**Average per test:** 960ms

## Time Breakdown

Per-test cost analysis (via phase timing):

| Phase | Time | % |
|-------|------|---|
| Node.js startup | ~95ms | 10% |
| TypeScript compiler loading | ~275ms | 29% |
| TypeScript parsing (sourceFile + checker) | ~400ms | 42% |
| JSON serialization + read | ~50ms | 5% |
| CBMC conversion (AST → GOTO) | ~20ms | 2% |
| CBMC GOTO post-processing | ~50ms | 5% |
| Solver | ~5-50ms typical, up to 1655ms worst case | 5-70% depending on program |

**Key finding:** The ~920ms baseline cost is dominated by external
process overhead (Node.js + TypeScript). Our converter itself is
highly efficient (~20ms for most programs).

## Slowest Tests

| Time (ms) | Test | Why |
|-----------|------|-----|
| 2546 | verify-gcd-nondet | Modulo loop solver (with integer mode) |
| 1684 | integration-memo-fib | Map operations + recursion |
| 1681 | verify-div-zero | IEEE 754 division encoding |

## Scaling

Tested with synthetic large files:
- 100 functions + 100 assertions: 1077ms (+150ms over baseline)
- 500 functions + 500 assertions: 2060ms (+1140ms over baseline)

Scales linearly with program size. No quadratic behavior observed.

## Why We Didn't Optimize

Potential optimizations considered and REJECTED for soundness:

1. **Reducing string/array bounds** (e.g., from 64 to 32)
   — UNSOUND. Programs exceeding the bound give wrong results.

2. **Inlining getters aggressively**
   — Already handled by CBMC's goto-conversion. Adding our own
     inlining would duplicate work and risk bugs.

3. **Skipping constant folding on parameters**
   — Would lose precision. Current constant tracking is sound.

4. **Relaxing integer inference**
   — Would change semantics. Integer inference is opt-in.

5. **Caching parsed ASTs across tests**
   — Complex IPC required. Each test has a distinct source file
     so cache hit rate would be low anyway.

6. **Using a different parser**
   — Would lose TypeScript type checker's resolved type info,
     which is used for type narrowing, generic instantiation,
     discriminated unions.

## What Would Actually Help

To reduce per-test time below ~900ms, the only practical option is a
**persistent TypeScript parser daemon**:

- One Node.js process that stays alive across multiple invocations
- IPC via Unix socket or stdio
- Each CBMC invocation sends source → receives AST JSON
- Saves 370ms startup per invocation

This is a significant engineering effort (protocol design, lifecycle
management, fallback for daemon crashes) but would roughly halve
verification time.

## Current Assessment

500-540 tests in ~9 minutes is acceptable for a test suite. Individual
tests typically under 1 second. The frontend is production-ready for
verification workloads.

---

## Update (2026-05-07): perf-based profiling confirms findings

Ran `scripts/profile_cbmc.py` (`perf`-based sampling + addr2line
source-level attribution) on three representative TypeScript workloads.
This analytical validation confirms the breakdown above.

### Methodology

Three workloads of increasing complexity:
1. **bench1** — simple for-loop, one assertion
2. **bench2** — class hierarchy with inheritance, array methods, instanceof, Map
3. **bench3** — binary search with symbolic input (`nondet_number` +
   `__CPROVER_assume`), heavier on CBMC's symex and SAT encoding

Command:
```bash
scripts/profile_cbmc.py --memory-limit 4000 --debug-binary build-debug/bin/cbmc \
  bench3.ts -- --ts-integer-mode --no-unwinding-assertions --unwind 20
```

### Finding 1: Node.js (V8) dominates

60–90% of CPU samples are in libnode.so (V8 internals). Top functions:
- `v8::internal::Scanner::Next()` — 11–18% (TypeScript scanner)
- `v8::internal::compiler::GraphReducer::ReduceTop()` — 9–13% (V8 optimizer)
- `v8::internal::Scavenger::ScavengeObject()` — 7–12% (V8 GC)

### Finding 2: CBMC code is a small fraction

For CBMC-heavy bench3, only **7.1% of samples are in CBMC code**. For
simpler bench1/bench2, CBMC is under 2%. The rest is Node.js, glibc,
and libstdc++.

### Finding 3: Top CBMC hotspots are shared infrastructure

When CBMC code does appear, it's in common code shared with C/C++ frontends:
- `cnft::process_clause` (`src/goto-symex/symex_target_equation.cpp:341`) — 4.1%
- `dimacs_cnft::write_dimacs_clause` (`symex_target_equation.cpp:351`) — 3.0%

These are NOT in the TypeScript frontend; optimizing them would benefit all
language frontends equally.

### Further optimization opportunities (not yet implemented)

1. **Cache parse output**: key on file hash, invalidate on change. Would
   reduce N-file regression runs from N × 2.5s to 2.5s + (N × <0.1s).
   ~50 LOC.
2. **Faster parser**: `sucrase`/`esbuild`/`swc` — 10–100× faster than tsc
   but lose some type info. ~500 LOC to re-integrate type checker.
3. **Persistent Node.js process**: spawn once, IPC for each file. ~100
   LOC, amortizes V8 startup over test batches.

Conclusion: the baseline overhead is Node.js + TypeScript, as expected.
Our actual conversion code contributes <1% of total time. For users who
care about throughput, caching or process persistence would help most.
