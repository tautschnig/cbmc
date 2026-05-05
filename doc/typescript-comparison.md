# TypeScript Frontend Comparison

Comparison of the CBMC TypeScript frontend with other TypeScript
verification approaches.

## CBMC TypeScript Frontend

| Aspect | Status |
|--------|--------|
| Approach | Bounded model checking via GOTO IR |
| Type system | Full TypeScript types (number, string, boolean, arrays, objects, classes, enums, unions, interfaces) |
| Verification | Exhaustive within bounds (sound with unwinding assertions) |
| Counterexamples | Full execution traces |
| Tests | 380 CORE, 13 KNOWNBUG |
| Performance | < 10s per test (typical 1-3s) |

## Feature Coverage

| Feature | Supported | Notes |
|---------|-----------|-------|
| Arithmetic | ✅ | All operators including **, %, bitwise |
| Strings | ✅ | 20+ methods (indexOf, replace, split, etc.) |
| Arrays | ✅ | 25+ methods (map, filter, reduce, find, etc.) |
| Classes | ✅ | Inheritance, static methods, multiple instances |
| Interfaces | ✅ | As struct types |
| Enums | ✅ | Regular and const enum |
| Union types | ✅ | Tagged unions with typeof narrowing |
| Generics | ❌ | Not yet implemented |
| Closures | Partial | Capture works; returning functions is KNOWNBUG |
| Multi-file | ✅ | TypeScript module resolution |
| Template literals | ✅ | With string/number interpolation |
| Destructuring | Partial | Array index access; rest not supported |
| Async/await | ❌ | Stripped (treated as sync) |

## Comparison with Other Approaches

### vs. TypeScript type checker (tsc)
- tsc: static type checking only, no runtime verification
- CBMC-TS: verifies runtime behavior (array bounds, assertions, arithmetic)

### vs. Property-based testing (fast-check)
- fast-check: random testing, may miss edge cases
- CBMC-TS: exhaustive within bounds, finds ALL counterexamples

### vs. Symbolic execution (SymJS, ExpoSE)
- SymJS/ExpoSE: JavaScript-focused, path explosion issues
- CBMC-TS: bounded model checking, handles loops via unwinding

### vs. Dafny/F*
- Dafny/F*: full verification with proof obligations
- CBMC-TS: push-button verification, no annotations needed (optional)

## Unique Strengths

1. **Push-button verification** — no annotations required for basic properties
2. **TypeScript-native** — uses TypeScript Compiler API for parsing
3. **Counterexample traces** — shows exact path to violation
4. **Nondet values** — model arbitrary inputs with constraints
5. **Instrumentation** — --bounds-check, --nan-check, --float-div-by-zero-check
6. **Integration** — same CBMC backend used for C/C++ verification
