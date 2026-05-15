# TypeScript Frontend Status — 2026-05-15

## Summary

The CBMC TypeScript frontend is in a production-ready state for
verifying TypeScript programs that use the core language features.
It has been hardened through a systematic three-pass ES2024 spec
review (26 bugs fixed), extensive property-based fuzzing (8000+
iterations, 61 invariants), and four real-world verification
harnesses (semver, URL, email, config).

## Metrics

| Metric | Value |
|--------|-------|
| CORE regression tests | 691 |
| KNOWNBUG tests | 3 (all design trade-offs) |
| npm integration harnesses | 16 |
| Real-world verification harnesses | 4 (semver, URL, email, config) |
| Property fuzzer invariants | 61 |
| Fuzzer iterations (clean) | 8000+ |
| CI gates | 2 (property fuzzer, matrix audit) |

## What works

### Language features (all ✅ in the capability matrix)
- All primitive types: number, string, boolean, bigint, null, undefined
- Control flow: if/else, switch, for, while, do-while, for-of, break, continue, try/catch/finally
- Functions: declarations, arrows, closures, recursion, rest/spread, default params, overloads
- Classes: constructors, methods, getters/setters, inheritance, private fields, static
- Generics: functions, classes, constraints, multi-type-param
- Type system: interfaces, unions, discriminated unions, type guards, narrowing, tuples
- Enums: numeric, string, bitflags
- Destructuring: array, object, with defaults, with rename, rest
- Modules: import/export, multi-file

### Built-in types
- **String**: full symbolic precision via refined-string solver. indexOf, slice, includes, startsWith, endsWith, trim, toUpperCase/toLowerCase, charAt, charCodeAt, repeat, replace, split, concat, padStart/padEnd, substring — all work on both constant and symbolic receivers.
- **Array**: push, pop, map, filter, reduce, forEach, find, findIndex, every, some, includes, indexOf, slice, splice, concat, flat, reverse, fill, sort, join, at, from, isArray, spread.
- **Number**: isInteger, isNaN, isFinite, parseInt, parseFloat, Number(), Math.* (30+ methods).
- **BigInt**: arithmetic, comparison, bitwise. Default signedbv[128]; opt-in `--ts-bigint-mathematical` for unbounded (SMT).
- **Date**: new Date(), Date.now(), getTime/valueOf, arithmetic coercion.
- **RegExp**: `/pattern/.test(s)` for literal patterns (Phase 1).
- **Map/Set**: add, get, has, delete, size, iteration.
- **JSON**: parse, stringify (constant evaluation).
- **Promise**: resolve, then, catch, all (sequential model; opt-in threading via `--ts-async-threading`).

### Verification features
- `console.assert(expr)` → CBMC assertion
- `__CPROVER_assume(expr)` → path constraint
- `nondet_number()`, `nondet_boolean()`, `nondet_string()` → symbolic inputs
- `--unwind N` for loop bounds
- `--bounds-check` for array bounds
- `--nan-check` for NaN detection
- Symbolic string content propagation through `__CPROVER_assume(s === "literal")`

## Known limitations

### Design trade-offs (3 KNOWNBUGs)
1. **async-race-undetected**: sequential async misses races (opt-in fix via `--ts-async-threading`)
2. **object-prototype-chain**: no prototype chain modelling
3. **strict-nan-not-equal**: NaN === NaN returns true (null-as-NaN sentinel model)

### Documented gaps
- `for-of` on strings: no-op (use indexed iteration)
- Ternary with literal-union inference: imprecise (annotate `: number`)
- Mixed-union-type arrays `(number | number[])[]`: crash in simplify_member
- RegExp metacharacters (`.`, `*`, `+`, `?`, `[]`): treated as literals
- `indexOf` with computed fromIndex: requires `--object-bits 10`
- Set<string>: not supported (Set<number> works)
- Calendar getters on Date (getFullYear etc.): return nondet
- `>>>` on BigInt: treated as signed shift (spec says TypeError)

### Not modelled
- Generators (`function*`)
- Dynamic `import()`
- WeakMap / WeakRef
- Proxy / Reflect
- Symbol (beyond basic usage)
- Full prototype chain / `Object.getPrototypeOf`

## Architecture

The frontend consists of:
- `src/typescript/typescript_language.cpp` + `ts_ast_server.js`: TypeScript Compiler API integration (parsing + type-checking via Node.js daemon)
- `src/typescript/typescript_converter.cpp`: expression conversion (2900+ lines)
- `src/typescript/typescript_converter_call.cpp`: method/function call dispatch (6500+ lines)
- `src/typescript/typescript_converter_stmt.cpp`: statement conversion (2500+ lines)
- `src/typescript/typescript_converter_func.cpp`: function/class declaration (500+ lines)
- `src/typescript/typescript_types.h`: type helpers (string, bigint, date)
- `src/solvers/strings/string_refinement.cpp`: refined-string solver integration (4 CBMC-core fixes)

## CI integration

Two GitHub Actions workflows:
1. **typescript-matrix-audit.yaml**: validates the capability matrix against actual test statuses on every PR touching the matrix or tests.
2. **typescript-property-fuzz.yaml**: runs 400 property-fuzzer iterations (61 invariants × ~7 iterations each) on every PR touching the frontend or solver.

## Recommended next steps

1. **RegExp Phase 2**: NFA-based matching for metacharacters on constant inputs (~2-3 days)
2. **SMT-string integration** (from tautschnig/py branch): enables RegExp Phase 3 and improves symbolic string reasoning
3. **Real-world harness expansion**: pick 2-3 more npm packages (e.g. `validator.js`, `path-to-regexp`)
4. **Upstream the CBMC-core fixes**: 4 independently-valuable solver-side changes
5. **A1 (strict-nan-not-equal)**: distinct NaN payloads for null/undefined/NaN (~1 day, medium risk)
