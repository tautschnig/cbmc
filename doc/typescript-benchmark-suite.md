# TypeScript Frontend Benchmark Suite

This document catalogs representative verification programs demonstrating
the TypeScript frontend's capabilities.

## Verification Categories

### 1. Arithmetic and Logic (10 programs)
| Test | Description | Technique |
|------|-------------|-----------|
| verify-absolute-diff | abs difference with nondet | Nondet + conditional |
| verify-modular-arith | constant modulo | Constant folding |
| verify-exponent | exponentiation operator | Constant evaluation |
| verify-power-of-two | bitwise AND trick | Bitwise ops |
| verify-xor-swap | XOR swap algorithm | Bitwise XOR |
| verify-bitwise-mask | byte extraction | Shift + mask |
| verify-boolean-logic | De Morgan's law | Nondet booleans |
| verify-nondet-clamp | ternary clamping | Nondet + ternary |
| verify-nondet-max | max of two nondet | Conditional |
| verify-nondet-ordering | transitivity | Assume chains |

### 2. Array Operations (12 programs)
| Test | Description | Technique |
|------|-------------|-----------|
| verify-array-sum-squares | map(x*x).reduce(sum) | Method chaining |
| verify-array-product | reduce with multiply | Reduce |
| verify-array-every-some | every() and some() | Predicate methods |
| verify-array-find-filter | find + filter | Search methods |
| verify-array-findIndex | findIndex method | Index search |
| verify-array-indexOf | indexOf + includes | Element search |
| verify-array-at | at() with negatives | Negative indexing |
| verify-array-reverse | reverse method | Array transform |
| verify-array-fill | fill method | Array mutation |
| verify-array-concat | concatenation | Array merge |
| verify-array-slice | slice with bounds | Subarray |
| verify-array-map-double | map doubling | Transform |

### 3. String Operations (8 programs)
| Test | Description | Technique |
|------|-------------|-----------|
| verify-string-methods-full | upper/lower/starts/ends | String methods |
| verify-string-ops-combined | length+indexOf+slice+replace | Combined |
| verify-string-indexOf-multi | multiple indexOf | Search |
| verify-string-replace-all | replaceAll | Transform |
| verify-string-repeat | repeat method | Generation |
| verify-string-slice-neg | negative slice | Negative index |
| verify-string-charAt | charAt | Character access |
| verify-string-split-length | split and length | Parsing |

### 4. Classes and Inheritance (8 programs)
| Test | Description | Technique |
|------|-------------|-----------|
| verify-class-counter-reset | counter with reset | State machine |
| verify-class-hierarchy | Shape → Rectangle | Inheritance |
| verify-class-inheritance-override | method override | Polymorphism |
| verify-class-two-instances | independent instances | Object identity |
| verify-class-method-chain | sequential methods | State mutation |
| verify-multi-class | multiple classes | Multi-type |
| verify-inheritance-method | parent method on child | Dispatch |
| static-method | static add/multiply | Static dispatch |

### 5. Control Flow (8 programs)
| Test | Description | Technique |
|------|-------------|-----------|
| verify-multi-return | 5 return paths | Branching |
| verify-ternary-chain | nested ternary | Conditional |
| verify-while-accumulate | while loop sum | Loop |
| verify-do-while | do-while doubling | Loop variant |
| verify-for-decrement | decrementing loop | Loop |
| verify-nondet-switch | switch on nondet | Switch |
| verify-const-enum | enum switch | Enum dispatch |
| verify-early-return | for with early return | Break |

### 6. Nondet Verification (6 programs)
| Test | Description | Technique |
|------|-------------|-----------|
| verify-nondet-array-property | array with assumptions | Constrained |
| verify-nondet-array-sum | bounded array sum | Bounded |
| verify-nondet-bounds | nondet index | Index bounds |
| verify-nondet-sign | sign extraction | Conditional |
| verify-abs-function | abs(n) === abs(-n) | Symmetry |
| verify-350-milestone | clamp with nondet | Range |

### 7. Verification Instrumentation (4 programs)
| Test | Description | Technique |
|------|-------------|-----------|
| nan-check-div | NaN check passes | --nan-check |
| nan-check-fail | NaN check catches 0/0 | --nan-check |
| bounds-check-fail | array OOB detected | --bounds-check |
| verify-div-zero | division by zero | --float-div-by-zero |

## Performance Characteristics

All 350 CORE tests complete within 10 seconds each. Typical verification
times:
- Simple arithmetic: < 1 second
- Array operations (length ≤ 16): 1-3 seconds
- Nondet with constraints: 1-5 seconds
- Recursive functions (unwind ≤ 12): 2-5 seconds

## Known Limitations (13 KNOWNBUGs)

| Category | Count | Description |
|----------|-------|-------------|
| String solver | 2 | charAt with nondet index, reverse+join |
| Runtime arrays | 3 | filter→map chain, filter.length, mutation in loops |
| Object model | 2 | Double spread override, cross-array access |
| Closures | 2 | Returning functions, function pointer in loop |
| Performance | 1 | Nondet GCD (modulo loop too slow) |
| Static factory | 1 | new in static method return |

---

## Final status (2026-05-06)

**500 CORE tests, 1 KNOWNBUG, 17 unit tests / 23 assertions, 165 commits**

Test categories (approximate counts):
- Arithmetic/logic: 50
- Array operations: 60
- String operations: 40
- Classes and inheritance: 50
- Generics: 5
- Map/Set: 10
- Control flow: 40
- Verification instrumentation: 10
- Nondet verification: 30
- Algorithms: 20
- State machines: 10
- Closures: 8
- Other: 167

All 500 tests complete within 30s; typical <3s each.
