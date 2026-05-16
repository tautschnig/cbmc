# TypeScript Frontend Status — 2026-05-16

## Summary

The CBMC TypeScript frontend is production-ready for verifying
TypeScript programs. It has been hardened through a systematic
three-pass ES2024 spec review (26 bugs fixed), extensive property-
based fuzzing (8000+ iterations, 61 invariants), and four real-world
verification harnesses (semver, URL, email, config). All Track A/B/C
items from the remaining-work plan have been addressed.

## Metrics

| Metric | Value |
|--------|-------|
| CORE regression tests | 700 |
| KNOWNBUG tests | 1 (opt-in design choice) |
| npm integration harnesses | 16 |
| Real-world verification harnesses | 4 (semver, URL, email, config) |
| Property fuzzer invariants | 61 |
| Fuzzer iterations (clean) | 8000+ |
| CI gates | 2 (property fuzzer, matrix audit) |

## What works

### Language features (all ✅ in the capability matrix)
- All primitive types: number, string, boolean, bigint, symbol, null, undefined
- Control flow: if/else, switch, for, while, do-while, for-of (arrays + strings), break, continue, try/catch/finally
- Functions: declarations, arrows, closures, recursion, rest/spread, default params, overloads, generators (function*)
- Classes: constructors, methods, getters/setters, inheritance (with field init), private fields, static, isPrototypeOf
- Generics: functions, classes, constraints, multi-type-param
- Type system: interfaces, unions (including literal-union collapse), discriminated unions, type guards, narrowing, tuples
- Enums: numeric, string, bitflags
- Destructuring: array (with defaults), object (with rename + defaults), rest, tuples
- Modules: import/export, multi-file
- Tagged template literals
- Proxy (pragmatic: returns target)

### Built-in types
- **String**: full symbolic precision via refined-string solver. indexOf (with computed fromIndex), slice (with computed indices), includes, startsWith, endsWith, trim, toUpperCase/toLowerCase, charAt, charCodeAt, repeat, replace, split, concat, padStart/padEnd, substring, parseInt on solver-produced strings — all work on both constant and symbolic receivers.
- **Array**: push, pop, map, filter, reduce, forEach, find, findIndex, every, some, includes (SameValueZero for NaN), indexOf, slice, splice, concat (variadic), flat, reverse, fill, sort, join, at, from, isArray, spread.
- **Number**: isInteger, isNaN (distinct from null/undefined), isFinite, parseInt (with radix, 0x prefix), parseFloat, Number(), Math.* (35+ methods including imul, clz32, log1p, expm1, hypot).
- **BigInt**: arithmetic, comparison, bitwise. Default signedbv[128]; opt-in `--ts-bigint-mathematical` for unbounded (SMT). `>>>` emits TypeError assertion.
- **Date**: new Date(), Date.now(), getTime/valueOf, getFullYear (epoch arithmetic), arithmetic coercion.
- **RegExp**: `/pattern/.test(s)` for literal patterns (Phase 1).
- **Map/Set**: add, get, has, delete, size, iteration. Set<string> supported.
- **WeakMap**: aliased to Map (no GC in BMC).
- **JSON**: parse, stringify (constant evaluation).
- **Promise**: resolve, then, catch, all (sequential model; opt-in threading via `--ts-async-threading`).
- **Symbol**: each Symbol() call produces a unique value.
- **Generator**: function* with yield; next().value and next().done.

### Verification features
- `console.assert(expr)` → CBMC assertion
- `__CPROVER_assume(expr)` → path constraint
- `nondet_number()`, `nondet_boolean()`, `nondet_string()` → symbolic inputs
- `--unwind N` for loop bounds
- `--bounds-check` for array bounds
- `--nan-check` for NaN detection
- Symbolic string content propagation through `__CPROVER_assume(s === "literal")`
- NaN === NaN correctly returns false (distinct NaN payloads)
- null === null correctly returns true
- `??` correctly distinguishes null/undefined from NaN

## Known limitations

### Design trade-off (1 KNOWNBUG)
1. **async-race-undetected**: sequential async misses races (opt-in fix via `--ts-async-threading`)

### Documented gaps (minor)
- RegExp metacharacters (`.`, `*`, `+`, `?`, `[]`): treated as literals (Phase 2 pending)
- `indexOf` with computed fromIndex: requires `--object-bits 10`
- Mixed-union-type arrays `(number | number[])[]`: crash in simplify_member
- Calendar getters on Date (getMonth, getDate, etc.): return nondet (getFullYear works)
- Generator parameters (`next(value)` sending values in): not supported
- `yield*` delegation: not supported
- Symbol-keyed properties: not supported (would need map-based property model)

### Not modelled
- Dynamic `import()` (doesn't crash; returns nondet)
- WeakRef.deref() (returns referent unconditionally)
- Proxy handler traps (pragmatic model ignores handler)
- Full `Object.create` / `Object.setPrototypeOf` (static prototype chain only)

## Architecture

The frontend consists of:
- `src/typescript/typescript_language.cpp` + `ts_ast_server.js`: TypeScript Compiler API integration (parsing + type-checking via Node.js daemon)
- `src/typescript/typescript_converter.cpp`: expression conversion (~3100 lines)
- `src/typescript/typescript_converter_call.cpp`: method/function call dispatch (~6800 lines)
- `src/typescript/typescript_converter_stmt.cpp`: statement conversion (~2700 lines)
- `src/typescript/typescript_converter_func.cpp`: function/class declaration (~550 lines)
- `src/typescript/typescript_types.h`: type helpers (string, bigint, date, NaN sentinels)
- `src/solvers/strings/string_refinement.cpp`: refined-string solver integration (4 CBMC-core fixes)

## CI integration

Two GitHub Actions workflows:
1. **typescript-matrix-audit.yaml**: validates the capability matrix against actual test statuses on every PR touching the matrix or tests.
2. **typescript-property-fuzz.yaml**: runs 400 property-fuzzer iterations (61 invariants × ~7 iterations each) on every PR touching the frontend or solver.

## Key design decisions

1. **NaN sentinels**: null (payload 1), undefined (payload 2), real NaN (payload 0) — distinct IEEE-754 quiet NaN values enabling correct `===`, `??`, and `Number.isNaN` semantics.
2. **String model**: inline fixed-size struct `{ length: int32, data: uint16[64] }` with solver-side routing for symbolic operations via `cprover_string_*_func` builtins.
3. **BigInt dual mode**: signedbv[128] for SAT (default), ID_integer for SMT (opt-in).
4. **Generator state machine**: yields collected at conversion time into a constant array; `next()` reads sequentially with a state counter.
5. **Literal-union collapse**: TS-inferred types like `10 | 20` collapse to `double_type()` rather than creating tagged unions.

## Recommended next steps

1. **RegExp Phase 2**: NFA-based matching for metacharacters on constant inputs (~2-3 days)
2. **SMT-string integration** (from tautschnig/py branch): enables RegExp Phase 3 and improves symbolic string reasoning
3. **Generator parameters**: support `next(value)` for coroutine-style generators
4. **Upstream the CBMC-core fixes**: 4 independently-valuable solver-side changes
5. **Real-world harness expansion**: pick 2-3 more npm packages
