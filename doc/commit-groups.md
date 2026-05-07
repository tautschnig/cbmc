# Logical Commit Groups

The `typescript-frontend` branch has 196 commits. For review purposes,
here's a logical grouping by feature area. This document catalogs the
work into ~25 coherent feature groups, which could be squashed into
a cleaner history if desired.

## How to view

- All commits: `git log --oneline typescript-frontend ^develop`
- Commits for a feature: `git log --oneline --grep="<keyword>" typescript-frontend ^develop`

## Feature Groups

### 1. Frontend infrastructure (7 commits)
- Language registration, parser integration, converter scaffold
- File layout and build system
- Keywords: frontend, scaffold, CMakeLists

### 2. Basic types and operators (15 commits)
- number, boolean, string, void, undefined
- Arithmetic, comparison, logical operators
- Keywords: convert_type, binary_expression

### 3. Statements and control flow (12 commits)
- if/else, while, for, return, break, continue
- switch/case, labeled break/continue
- try/catch/throw
- Keywords: statement, control flow

### 4. Classes and inheritance (14 commits)
- Constructors, methods, instance methods
- Static methods, inheritance (extends, super)
- Private fields with access control
- Getters/setters
- Keywords: class, method, inheritance, private

### 5. Functions and closures (10 commits)
- Function declarations, parameters, return types
- Arrow functions, function expressions
- Closures (value and reference capture)
- Default parameters, rest parameters
- Higher-order functions
- Keywords: function, closure, arrow, rest

### 6. Arrays (20 commits)
- Array literals, length, indexing
- 30+ methods: map, filter, reduce, find, every, some,
  indexOf, includes, slice, concat, fill, reverse, join,
  push, pop, splice, at, flat, findIndex, etc.
- Array destructuring with rest
- Spread in array literals
- Keywords: array, map, filter, reduce

### 7. Strings (15 commits)
- String literals, length, indexing
- 20+ methods: indexOf, substring, replace, split, repeat,
  trim, charAt, startsWith, endsWith, toUpperCase, toLowerCase,
  padStart, padEnd, slice, replaceAll, etc.
- Template literals with interpolation
- String migration to inline array model
- Non-constant indexOf/substring (symbolic)
- Keywords: string, template, substring, indexOf

### 8. Objects (12 commits)
- Object literals, property access
- Spread syntax, computed properties, shorthand
- Object destructuring
- Nested objects
- Nested property access
- Keywords: object, spread, destructuring

### 9. Union types and narrowing (8 commits)
- Tagged unions, typeof narrowing
- Discriminated unions with object members
- Type guards (x is Foo)
- Union assignment construction
- Keywords: union, narrowing, discriminated

### 10. Generics (7 commits)
- Generic functions (monomorphization)
- Generic classes (per-instantiation specialization)
- Generic constraints (T extends I)
- Type parameter resolution
- Keywords: generic, monomorphize

### 11. Map and Set (6 commits)
- Map<K,V> with bounded storage
- Set<T> with bounded storage
- set/add/get/has/delete/size operations
- for-of iteration over entries
- Linear scan for constant key lookup
- Keywords: Map, Set, iteration

### 12. Async and Promises (5 commits)
- async/await treated as synchronous
- Promise.resolve, then, catch, finally
- Keywords: async, await, promise

### 13. Type aliases and utility types (6 commits)
- Type aliases via TypeScript checker resolution
- Pick, Omit, Readonly utility types
- Template literal types
- String literal types
- Keywords: alias, utility, Pick, Omit

### 14. Verification primitives (6 commits)
- __CPROVER_assume, assert, cover
- __CPROVER_loop_invariant
- __CPROVER_requires, ensures
- nondet_number, nondet_string, nondet_boolean, nondet_array
- Keywords: CPROVER, nondet

### 15. Verification flags (4 commits)
- --bounds-check
- --float-div-by-zero-check
- --nan-check
- --ts-integer-mode (opt-in, signedbv[64])
- --ts-max-array-size
- Keywords: flag, option, integer-mode

### 16. Decorators (2 commits)
- Basic decorators
- Parameterized decorators
- Keywords: decorator

### 17. Namespaces (1 commit)
- ModuleDeclaration handling
- Namespace.function() dispatch
- Keywords: namespace

### 18. JSX/TSX (1 commit)
- .tsx extension support
- JsxEmit.React parser option
- Keywords: tsx, jsx

### 19. Multi-file support (3 commits)
- Import/export resolution
- Multi-file compilation via TypeScript program API
- Keywords: import, module, multi-file

### 20. Error messages and traces (3 commits)
- expr2typescript module for counterexample formatting
- Improved warning messages with suggestions
- Struct/string/array literal formatting
- Keywords: error, trace, expr2typescript

### 21. Closure semantics (4 commits)
- Free variable scan for capture
- Function pointer types
- Return closure from function (closure binding map)
- Multi-closure shared mutable state
- Keywords: closure, capture, binding

### 22. Converter splitting and refactoring (4 commits)
- Split typescript_converter.cpp into 4 files
- Reduce duplicate code (helpers extracted)
- clang-format-15 compliance
- Keywords: refactor, split, cleanup

### 23. Documentation (8 commits)
- Verification guide
- Benchmark suite
- Semantic compliance review
- Architecture decisions
- Frontend architecture + new frontend guide
- Performance report
- tsc vs CBMC comparison
- Keywords: doc, guide, architecture

### 24. Testing (20+ commits)
- 550 regression tests
- 17 unit tests
- Integration tests (memo-fib, URL parser, event emitter, etc.)
- Keywords: test, regression, unit

### 25. Cross-references (2 commits)
- 135 ES2024 section references
- TypeScript Handbook references
- Keywords: xref, reference, ES2024

## Squashing Strategy (if desired)

For upstream merge, the 196 commits could be squashed to ~25 logical commits:

```bash
git rebase -i develop
# In the interactive editor, mark commits after the first in each group as 'squash'
```

This preserves logical grouping while reducing history clutter.

Alternative: keep the detailed history for traceability, since each
commit has a clear purpose and passes tests. Large projects often
prefer detailed history for code archaeology.
