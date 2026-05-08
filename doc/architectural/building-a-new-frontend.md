# Building a New CBMC Frontend

A step-by-step guide for adding support for a new programming language
to CBMC. This document is based on real experience building the Python
and TypeScript frontends (the two most recent additions).

For the conceptual architecture, see `cbmc-frontend-architecture.md`.

## Table of Contents

1. [Is CBMC the Right Tool?](#is-cbmc-the-right-tool)
2. [Before You Start](#before-you-start)
3. [Choosing Your Approach](#choosing-your-approach)
4. [Step-by-Step Implementation](#step-by-step-implementation)
5. [Testing Strategy](#testing-strategy)
   - Start small
   - Add feature by feature
   - Use KNOWNBUG status
   - Test verification is actually happening
   - Integration test with real code
   - **Per-spec soundness review** (highly recommended)
     - First pass: constant-input probes
     - Second pass: symbolic-input probes
6. [Common Pitfalls](#common-pitfalls)
7. [Performance and Soundness](#performance-and-soundness)
8. [Maintenance Considerations](#maintenance-considerations)

---

## Is CBMC the Right Tool?

CBMC is a bounded model checker. It verifies properties **up to a
depth bound**. It's great for:

- Finding bugs in programs with concrete or small-input behaviors
- Proving assertion correctness over bounded loops
- Exhaustive path exploration within a fixed program execution depth

It's **not** great for:

- Proving correctness for all possible inputs (use theorem provers
  like Coq, Lean, Isabelle instead)
- Languages with highly dynamic dispatch that's irreducible at compile
  time
- Runtime monitoring or fuzzing (use other tools)
- Reasoning about infinite streams, lazy evaluation, or coinductive
  structures

**Before investing in a frontend, consider**: can you express your
verification problems in bounded form?

---

## Before You Start

### Prerequisites

1. **Familiarity with your target language** — including its parser
   ecosystem, standard library, and common idioms
2. **Familiarity with CBMC basics** — build system, test framework
3. **C++ skills** — CBMC is C++17
4. **Understanding of GOTO programs** — read `doc/architectural/`

### What to read first

1. `doc/architectural/cbmc-architecture.html` — overview
2. `doc/architectural/central-data-structures.md` — core types
3. `src/langapi/language.h` — the interface you'll implement
4. An existing frontend that's close to your target:
   - If your language has similar semantics to C: read `src/ansi-c/`
   - If it's dynamic (Python-like): read `src/python/`
   - If it has a nice parser API (TypeScript, Kotlin): read
     `src/typescript/`

---

## Choosing Your Approach

### Approach 1: External parser via subprocess

Use when:
- Language has an official parser with a clean AST API
- You want fast time-to-prototype
- The AST is serializable to JSON or similar

Examples: Python (`src/python/`), TypeScript (`src/typescript/`)

**Pros:**
- Leverage battle-tested parser (handles edge cases)
- Type information provided by language tooling
- Language updates propagate automatically

**Cons:**
- Process startup overhead per file
- JSON serialization cost
- Need to install language tooling as dependency

**Skeleton:**
```cpp
class mylanguaget : public languaget {
public:
  bool parse(std::istream &in, const std::string &path,
             message_handlert &mh) override {
    // 1. Write external parser script to temp file
    std::string script_path = write_parser_script();
    // 2. Run parser subprocess, save JSON to temp file
    std::string json_path = write_json_temp();
    int result = run("language_runtime",
                     {"language_runtime", script_path, path, json_path},
                     "", "", stderr_path);
    // 3. Read JSON back
    parse_json(json_path, mh, ast_json);
    return result != 0;
  }

  bool typecheck(symbol_table_baset &symbol_table,
                 const std::string &module,
                 message_handlert &mh) override {
    mylanguage_convertert converter{symbol_table, filename, ast_json, mh};
    return converter.convert();
  }
  // ... other methods
};
```

### Approach 2: Built-in parser (flex/bison)

Use when:
- Language has stable, well-specified grammar
- You need full control over parsing
- No suitable external parser exists

Examples: C (`src/ansi-c/`), C++ (`src/cpp/`)

**Pros:**
- No runtime dependencies
- Fast parsing
- Full access to parse state for error recovery

**Cons:**
- Substantial implementation effort (weeks to months)
- Must track language evolution manually
- Complex grammars (C++!) are hard to maintain

### Approach 3: Library integration

Use when:
- A mature parsing library exists as C/C++
- Language format is not source code (e.g., bytecode, WASM)

Examples: Java bytecode (`jbmc/src/java_bytecode/`) parses .class
files directly.

---

## Step-by-Step Implementation

### Step 1: Create the directory structure

```
src/mylanguage/
├── CMakeLists.txt          # Build configuration
├── module_dependencies.txt # Dependencies (util, goto-programs, langapi)
├── mylanguage_language.h   # Language class declaration
├── mylanguage_language.cpp # Parse, typecheck, registration
├── mylanguage_converter.h  # AST → GOTO converter
├── mylanguage_converter.cpp
├── mylanguage_types.h      # Type helpers (optional)
├── expr2mylanguage.h       # Formatting for traces (optional)
└── expr2mylanguage.cpp
```

Register in parent `CMakeLists.txt` and `src/langapi/mode.cpp`.

### Step 2: Implement `languaget`

Start minimal: just parse a file and produce an empty symbol table.

```cpp
// mylanguage_language.h
class mylanguaget : public languaget {
public:
  bool parse(std::istream &, const std::string &,
             message_handlert &) override;
  bool typecheck(symbol_table_baset &, const std::string &,
                 message_handlert &) override;
  std::string id() const override { return "mylanguage"; }
  std::string description() const override {
    return "My Language";
  }
  std::set<std::string> extensions() const override {
    return {"myl"};
  }
  // ... other methods with default implementations
};

std::unique_ptr<languaget> new_mylanguage_language();
```

### Step 3: Build converter for expressions

Start with the simplest expressions:
- Literals: numbers, strings, booleans
- Identifiers: look up symbols
- Binary operators: +, -, *, /, <, >, ==, !=
- Function calls

```cpp
// In converter:
exprt convert_expression(const ast_node_t &node) {
  switch (node.kind) {
    case NUMBER_LITERAL:
      return from_integer(node.value, signedbv_typet{64});
    case STRING_LITERAL:
      return convert_string_literal(node.text);
    case IDENTIFIER:
      return lookup_symbol(node.name);
    case BINARY_EXPR:
      return convert_binary(node);
    case CALL_EXPR:
      return convert_call(node);
    // ...
  }
}
```

### Step 4: Build converter for statements

- Variable declarations (add to symbol table)
- Assignments
- If/else, while, for
- Return
- Function calls as statements

```cpp
codet convert_statement(const ast_node_t &node) {
  switch (node.kind) {
    case VAR_DECL: {
      // Create symbol, assign initial value
      symbolt sym{sym_id, convert_type(node.type), "mylanguage"};
      symbol_table.add(sym);
      if (node.init) {
        return code_frontend_assignt{
          symbol_exprt{sym_id, sym.type}, convert_expression(*node.init)};
      }
      return code_skipt{};
    }
    case IF_STMT:
      return code_ifthenelset{
        convert_expression(node.cond),
        convert_statement(node.then_branch),
        convert_statement(node.else_branch)};
    // ...
  }
}
```

### Step 5: Build the entry point

```cpp
void convert_module() {
  // For each top-level statement:
  //   - If function: register function, convert body
  //   - If global var: register, convert initializer
  //   - If statement: add to __CPROVER_start body

  code_blockt start_body;
  for (const auto &stmt : parsed_module.statements) {
    if (is_function_decl(stmt)) {
      convert_function(stmt);
    } else {
      start_body.add(convert_statement(stmt));
    }
  }

  // Create __CPROVER_start function
  symbolt start{"__CPROVER__start", code_typet{{}, empty_typet{}}, "mylanguage"};
  start.value = start_body;
  symbol_table.add(start);
}
```

### Step 6: Add source locations

For counterexample traces, every symbol, expression, and statement
should have source location info:

```cpp
source_locationt get_location(const ast_node_t &node) {
  source_locationt loc;
  loc.set_file(current_filename);
  loc.set_line(std::to_string(node.line));
  loc.set_column(std::to_string(node.col));
  return loc;
}

// Use it:
exprt e = convert_expression(node);
e.add_source_location() = get_location(node);
```

### Step 7: Implement `from_expr` and `from_type`

These format expressions/types in your language's syntax for counter-
examples. Simple for most constructs; see `src/typescript/expr2typescript.cpp`
for an example.

```cpp
std::string expr2mylanguage(const exprt &e, const namespacet &ns) {
  if (e.id() == ID_constant) return format_constant(e);
  if (e.id() == ID_symbol) return pretty_name(to_symbol_expr(e));
  if (e.id() == ID_plus) return recurse(e[0]) + " + " + recurse(e[1]);
  // ...
  return "(" + id2string(e.id()) + ")"; // fallback
}
```

### Step 8: Add verification primitives

Recognize special function names:
- `__CPROVER_assume(cond)` → `code_assumet{cond}`
- `__CPROVER_assert(cond, msg)` → `code_assertt{cond}`
- `nondet_<type>()` → `side_effect_expr_nondett{type, loc}`

```cpp
exprt convert_call(const call_node_t &call) {
  std::string fname = call.callee_name;
  if (fname == "__CPROVER_assume" && call.args.size() == 1) {
    pending_stmts.push_back(
      code_assumet{convert_expression(call.args[0])});
    return nil_exprt{};
  }
  // ... regular function call
}
```

### Step 9: Write regression tests

Create `regression/mylanguage/<test_name>/`:
```
main.myl     # your test program
test.desc    # expected results
```

`test.desc` format:
```
CORE
main.myl

^EXIT=0$
^SIGNAL=0$
^VERIFICATION SUCCESSFUL$
--
```

Run: `cd regression/mylanguage && perl ../test.pl -e -p -c ../../build/bin/cbmc -C`

### Step 10: Add unit tests

Create `unit/mylanguage/mylanguage_test.cpp` using Catch2:
```cpp
TEST_CASE("convert_type: int", "[mylanguage]") {
  symbol_tablet st;
  null_message_handlert mh;
  jsont empty;
  mylanguage_convertert c{st, "test.myl", empty, mh};
  typet t = c.convert_type("int");
  REQUIRE(t.id() == ID_signedbv);
}
```

Run: `./build/bin/unit "[mylanguage]"`

---

## Testing Strategy

### Start small

- Arithmetic: `const x = 1 + 2; assert(x == 3);`
- Variables: `let x = 5; assert(x === 5);`
- Functions: `function f(x) { return x + 1; } assert(f(5) === 6);`
- Control flow: if/else, while loop

### Add feature by feature

For each language feature, add 2-5 tests:
- Positive (verifies correctly)
- Negative (verification fails as expected)
- Edge case (boundary behavior)

### Use `KNOWNBUG` status

For features you haven't implemented yet, mark tests as KNOWNBUG:
```
KNOWNBUG
main.myl

^EXIT=0$
^SIGNAL=0$
^VERIFICATION SUCCESSFUL$
--
Description of what's not yet implemented
```

CBMC's test runner distinguishes KNOWNBUG from actual failures.

### Test verification is actually happening

Watch out for tests that "pass" because assertions are silently
dropped (e.g., due to unsupported expressions returning nondet).
Always grep the output for `assertion: SUCCESS` to confirm.

### Integration test with real code

Once your frontend works on synthetic tests, try a real program
from your language's ecosystem. This surfaces gaps you didn't
anticipate.

### Per-spec soundness review (highly recommended)

Hand-written tests tend to exercise what the implementer was
thinking about. They do not reliably find the bugs that come from
misreading a spec section. After the frontend is working on common
cases, do a **systematic per-spec review** of each major built-in
subsystem. This methodology has been the most productive quality
technique in building the TypeScript frontend.

#### The methodology

For each major spec section (e.g. ES2024 §22.1 String,
§23.1 Array, §21.3 Math, §24.1 Map, §24.2 Set, §21.1 Number):

1. **Enumerate the methods and properties in spec order.** Read
   each `§X.X.Y` heading and list what it says.
2. **Write probe tests that exercise BOUNDARY and EDGE behaviours**
   — not just the happy path. For each method:
   - Default arguments (what happens when arg omitted?)
   - Negative indices (count from end? clamp? error?)
   - Indices past length (clamp or out-of-bounds?)
   - Empty receivers (`""`, `[]`, `new Map()`, 0)
   - Short-circuit paths (already-satisfied postcondition)
   - Spec-mandated quirks (tie-breaking direction, arg swap)
3. **Run and collect failures.** Many tests will pass; expect 2–10
   failures per subsystem if your frontend is in "works for common
   cases" state.
4. **Fix each failure in a small focused commit.** The small scope
   means each commit is easy to review and revert.
5. **Promote probe tests to CORE regression tests.** They become
   the regression guards.
6. **Second pass: symbolic inputs.** After the constant-input tests
   pass, re-probe each subsystem with `nondet_number()` /
   `nondet_string()` / `__CPROVER_assume` inputs. This catches
   implementations that are sound for constants but fall through
   to nondet (or produce wrong results) for symbolic arguments.
   Many method implementations start by extracting the constant
   value of an argument — when that extraction fails, they return
   nondet, silently degrading soundness for property-based tests.
   See "Symbolic-input pass" below for details.
7. **Document findings** in a per-subsystem review file so future
   maintainers can see what was checked and what remains.

#### Example yield (TypeScript frontend)

Per-spec reviews on a "works for common cases" frontend:

| Subsystem | Real bugs | Missing methods | New CORE tests | Time |
|-----------|-----------|-----------------|----------------|------|
| String (§22.1) | 6 | 4 | 11 | ~90 min |
| Array (§23.1) | 4 | 3 | 7 | ~60 min |
| Math (§21.3) | 2 | 10 | 4 | ~45 min |
| Number (§21.1) | 3 | 6 | 4 | ~45 min |
| Map/Set (§24) | 4 | 3 | 2 | ~30 min |

Pattern: the review consistently finds several bugs per subsystem
that hand-written tests had missed — typically around boundary
semantics, default arguments, and language-specific quirks.

#### Classic bug patterns that per-spec review catches

Based on findings across multiple reviews:

1. **Tie-breaking direction wrong.** Example: `Math.round(-0.5)` —
   `std::round` rounds ties away from zero (so `-0.5 → -1`), but
   ES2024 §21.3.2.29 rounds ties toward +infinity (so `-0.5 → 0`).
   Subtle; only exposed by testing `-0.5`, `-2.5` specifically.

2. **Ignored secondary arguments.** Many spec methods have an
   optional second argument (`fromIndex`, `position`, `endPosition`,
   `padChar`, `separator`). Implementations often handle the first
   arg and ignore the rest. Test each with and without it.

3. **Argument-order swap per spec.** `substring(start, end)` per
   ES2024 §22.1.3.21 swaps args when `start > end`. Almost nobody
   implements this without the spec saying so explicitly.

4. **Short-circuit paths reversed.** Example: `"hello".padStart(2)` —
   the string is already longer than the target, so the spec says
   "return unchanged". An implementation that unconditionally
   slices instead will return `"lo"` (last 2 chars).

5. **Missing set-membership semantics.** `Set.add` is often
   implemented as append-to-array, which allows duplicates.
   Spec §24.2.3.1 says adding an existing element is a no-op.
   Easy to test with `add(1); add(1); console.assert(size === 1)`.

6. **Non-numeric arguments to numeric predicates.** ES2024
   §21.1.2.4: `Number.isNaN` does NOT coerce — non-numbers must
   return false. Implementations using underlying `isnan` primitives
   will return true for any non-float input.

7. **Incorrect mutation semantics.** `Array.fill`, `Array.splice`,
   `Array.sort` must mutate in place. Implementations that return
   a new array without updating the receiver pass incorrect tests
   like `arr.fill(0); console.assert(arr[0] === 0)`.

8. **Empty-receiver bypass.** String method dispatchers often guard
   on `!sv.empty()`, which silently skips every method when the
   receiver is `""`. Test with empty receivers separately.

9. **Pattern truncation in iterative building.** Example: `padStart`
   with multi-char pad. The spec requires truncating the built pad
   string to exactly `(target - source)` chars, not truncating the
   final result. Off-by-one errors are common.

10. **Missing spec-level edge cases.** `Array.isArray("x")` must
    return false. Type guards that check "has length property"
    incorrectly return true for strings.

#### What to ship alongside the review

Each subsystem review should produce:

- **Per-subsystem review document** (e.g. `doc/string-soundness-review.md`)
  listing methods reviewed, bugs found, methods added, and remaining
  gaps.
- **CORE tests** for each fixed bug (one per symptom, so regressions
  are clearly attributed).
- **Capability matrix entries** cross-referenced to the tests.
- **Commit per fix** with spec citation in the message
  (e.g. `ES2024 §22.1.3.21` in the commit message and source comment).

#### When to do per-spec reviews

- **After initial feature completeness**: once the common cases
  work, review finds the edges.
- **Before integration testing**: catches bugs that real code would
  trip over.
- **Periodically after language spec updates**: re-review the
  sections whose spec changed.

#### Symbolic-input pass (second pass)

The first pass (above) catches spec-misreading bugs using constant
inputs. The second pass catches a different class of bug: **methods
that are correct for constants but degrade to nondet for symbolic
inputs**.

Why this matters: a common implementation pattern is to extract the
constant value of an argument at conversion time, use it to compute
the result at conversion time, and return a constant. When the
argument is symbolic, the extraction fails and the handler falls
through to `side_effect_expr_nondett{...}` — technically sound
(returns "any value") but silently breaks any property-based test
that uses `nondet_number()` inputs.

**The symbolic-input pass methodology:**

1. For each method reviewed in the first pass, write a probe test
   that passes `nondet_number()` / `nondet_string()` / etc. to
   the method's arguments (or receiver).
2. Add `__CPROVER_assume` constraints to bound the search space.
3. Assert the invariants the method should satisfy
   (e.g. `Math.abs(x) >= 0`, `a.slice(0, k).length === k`).
4. For failing tests, examine the implementation: does it take a
   conversion-time-constant path that falls through to nondet? If
   so, add a symbolic fallback using CBMC's primitives:
   - Comparisons: `binary_relation_exprt{a, ID_lt, b}`
   - Conditional: `if_exprt{cond, true_val, false_val}`
   - NaN check: `isnan_exprt{x}` or `ieee_float_notequal_exprt{x, x}`
   - Finite check: `not_exprt{or_exprt{isnan_exprt{x}, isinf_exprt{x}}}`
5. For methods where a symbolic encoding is genuinely hard (e.g.
   variable-length result arrays, float-to-int typecast semantics,
   regex matching), leave them as nondet and **document the
   limitation** in a test file with a `KNOWNBUG` or an explicit
   code comment. Don't pretend they work symbolically.

**Findings from the TypeScript frontend symbolic-input pass:**

| Subsystem | Symbolic-safe methods | Documented limitations |
|-----------|----------------------|------------------------|
| Math | abs, sign, max, min | floor/ceil/trunc (float→int typecast issues) |
| Number | isNaN, isFinite | isInteger/isSafeInteger (same typecast issue) |
| String | length, includes, startsWith, endsWith on symbolic substring | repeat/padStart with symbolic count (variable-length result) |
| Array | `a[i]` access, `a.includes(y)` | slice/splice/fill with symbolic args |
| Map/Set | set/get/has/delete/add on symbolic values | (none — linear scan works symbolically) |

**Key soundness finding:** `nondet_X()` primitives should emit
bounded values, not fully unconstrained. For example, our initial
`nondet_string()` returned a string with a nondet `length` field
(which could be negative), making length-based assertions unsound.
Fix: emit a `__CPROVER_assume` bounding the length to `[0, MAX]`.

Every `nondet_X()` primitive should be audited for similar issues.

**Pattern to recognise in implementation code:**

```cpp
// Pattern that silently degrades for symbolic input:
if (constant_value_extraction_succeeds) {
  return compute_constant_result();
}
return side_effect_expr_nondett{...};  // silently nondet
```

Change to:

```cpp
if (constant_value_extraction_succeeds) {
  return compute_constant_result();
}
// Symbolic fallback: emit the operation as CBMC primitives
return build_symbolic_expression(...);
```

Or, if no sound symbolic encoding exists:

```cpp
if (constant_value_extraction_succeeds) {
  return compute_constant_result();
}
// Symbolic path: documented limitation (see subsystem-soundness-review.md).
// Users needing this symbolically should ... (workaround).
return side_effect_expr_nondett{...};
```

The explicit "documented limitation" comment prevents future
maintainers from thinking the symbolic case is covered when it
isn't.

#### Check CBMC primitives before documenting as a limitation

When you encounter an operation that "seems hard" to do symbolically,
**check the C frontend** and CBMC's expression types before declaring
it a fundamental limitation. CBMC has purpose-built expression types
for most numeric and float operations.

Useful CBMC primitives to check (all in `src/util/`):

| Primitive | Use for |
|-----------|---------|
| `floatbv_round_to_integral_exprt{x, mode}` | floor / ceil / trunc / round (modes: FE_DOWNWARD=1, FE_UPWARD=2, FE_TOWARDZERO=3, FE_TONEAREST=0) |
| `floatbv_typecast_exprt{x, rm, t}` | float → float precision conversion |
| `ieee_float_equal_exprt`, `ieee_float_notequal_exprt` | IEEE equality (handles NaN) |
| `isnan_exprt`, `isinf_exprt` | special-value checks |
| `if_exprt{cond, then, else}` | conditional expression |
| `binary_relation_exprt{a, ID_lt/le/gt/ge, b}` | comparisons |

The C frontend's `src/ansi-c/library/math.c` shows how all the
standard math functions are encoded. Copy the pattern.

**Example**: we initially documented `Math.floor` on symbolic input
as an unavoidable limitation, citing "float-to-int typecast issues".
On checking the C frontend, `floor(x)` is just
`__CPROVER_round_to_integrald(x, 1)`, which becomes
`floatbv_round_to_integral_exprt` — a CBMC primitive that handles
the symbolic case natively. The "limitation" was actually premature.

For bounded collections (arrays, maps, sets) with symbolic inputs,
the **per-slot if_exprt pattern** works well:

```cpp
// For arr.indexOf(target) with symbolic target:
exprt result = make_int(-1);
for (int i = src_len - 1; i >= 0; i--) {
  result = if_exprt{
    equal_exprt{arr.data[i], target},
    make_int(i),
    result
  };
}
return result;
```

**For variable-length string operations** (repeat with symbolic
count, padStart with symbolic target length, indexOf with symbolic
needle, parseFloat with symbolic string), CBMC has a **refined
string solver** at `src/solvers/strings/` with better handling.
Integrating it into a frontend is a larger project; for now,
document the limitation.

#### Order the subsystems

Prioritize by user impact:

1. **Primitives first** (number, string, boolean)
2. **Most-used collections** (array, then map/set)
3. **Math / type guards** (Math, Number.is*)
4. **Async and error handling** (Promise, Error, try/catch)
5. **Modules and classes** (import/export, classes, inheritance)
6. **Less-used built-ins** (JSON, Object, Date, RegExp)

Reviewing in this order ensures high-frequency code paths get the
most attention first.

---

## Common Pitfalls

### 1. Silently dropping features → false verification

If your converter returns `nondet_exprt` for unsupported constructs,
verification might "pass" trivially. Instead:
- Log a warning: `log.warning() << "Unsupported: ..."`
- Return nondet with explicit source location
- Document which features are supported

### 2. Type mismatches

CBMC strict-types almost everything. If you assign a `signedbv[64]`
to a variable of type `signedbv[32]`, you need an explicit
`typecast_exprt`. Missing casts cause mysterious crashes in the
solver or goto-conversion.

```cpp
if (rhs.type() != lhs.type()) {
  rhs = typecast_exprt{rhs, lhs.type()};
}
```

### 3. Symbol name collisions

Use fully qualified names: `mylanguage::module::function::local_var`.
Avoid reusing names across scopes.

### 4. Source locations

Forgetting `add_source_location()` makes counterexamples useless.
Always tag conversions.

### 5. Unbounded structures

Dynamic languages have unbounded lists, strings, dicts. Decide your
bound early and document it. Don't silently truncate (unsound).

### 6. Generic types

If your language has generics (Java, TypeScript), decide early:
- **Monomorphization**: specialize per instantiation (clean but more code)
- **Type erasure**: treat T as Object (simpler but loses type info)
- **Union tags**: dispatch on runtime type (most flexible)

Mixing approaches leads to bugs.

### 7. Integer vs float semantics

JavaScript/TypeScript numbers are IEEE 754 doubles. Python integers
are arbitrary precision. Java has int/long/etc. Each requires
different modeling.

For performance, you might model "integer-looking" numbers as
`signedbv[64]` instead of `floatbv[64]` — but this changes semantics!
Only do this as an opt-in flag with soundness caveats.

### 8. Circular dependencies

`convert_type("MyAlias")` might recurse infinitely if `MyAlias = MyAlias`.
Add cycle detection or cache.

### 9. Over-aggressive optimization

Don't optimize during conversion unless you're certain it's sound.
CBMC's own optimizer handles many cases. Better to produce verbose,
obviously-correct code and let CBMC optimize it.

### 10. Forgetting to handle imports/modules

Multi-file programs need: module resolution, cross-file symbol
references, proper scoping. Plan this upfront — retrofitting is painful.

### 11. Trusting hand-written tests to cover the spec

Hand-written tests exercise what the implementer was thinking about.
They systematically miss the edge cases that come from misreading a
spec section (tie-breaking direction, negative index semantics,
default argument values, short-circuit paths). A frontend that passes
200 hand-written tests can still have dozens of spec-mandated bugs
waiting.

**Mitigation:** do a per-spec review (see Testing Strategy §) before
calling any subsystem "done". In practice, per-spec reviews consistently
find 2–10 bugs per subsystem even after the hand-written tests all pass.

### 12. Tests that silently skip the assertion

Some tests pass not because their assertions hold but because the
assertions are NEVER CHECKED. Causes include:

- CBMC flags that disable assertions broadly (e.g.
  `--no-built-in-assertions` disables BOTH CBMC-generated checks and
  user `assert()` calls in some frontends).
- Early exits: a `throw` or `return` before the assertion makes it
  unreachable.
- Silently-dropped expressions: if `convert_expression(cond)` returns
  `nil_exprt` because `cond` uses an unsupported feature, the test
  runner may omit the assertion entirely.

**Mitigation: mutation-test the regression suite.** Apply generic
mutations (swap `===` for `!==`, flip `true`/`false`, negate
assertions) to each test and require the mutated test to FAIL. If a
mutation doesn't change the verification outcome, the assertion
likely isn't being checked.

A simple mutation-testing script is a worthwhile automation:

```python
# For each test.desc CORE file:
for mutation in MUTATIONS:
  mutated_src = apply_mutation(original_src, mutation)
  run_cbmc(mutated_src)
  assert "VERIFICATION FAILED"  # mutation caught
```

Running such a script on the TypeScript frontend's ~600 CORE tests
surfaced real issues: heterogeneous-tuple tests passed vacuously
(tuple type annotation wasn't parsed, assertions ran on nondet
values), and `--no-built-in-assertions` was accidentally silencing
user assertions.

---

## Performance and Soundness

### The bound trade-off

Every bound you set is a trade-off:
- **Small bound** → fast verification, but can miss bugs beyond bound
- **Large bound** → thorough, but slow or infeasible

Make bounds:
- Explicit (documented numeric constant)
- Configurable (command-line flag)
- Monitored (test suite exercises realistic bounds)

### Path explosion

Nondet values multiply paths. A function with 5 nondet booleans has
2^5 = 32 paths. CBMC handles this via SAT, but it scales poorly for
many nondet + loops.

**Mitigations:**
- Add strong preconditions via `__CPROVER_assume`
- Use smaller bounded loops
- Use bounded types when possible (signed int < IEEE float complexity)

### Soundness checklist

Before calling your frontend "sound":
- [ ] Every unsupported feature returns nondet or errors out (not silent)
- [ ] All bounds are explicit and documented
- [ ] All typecasts preserve values or are explicitly unsound (opt-in)
- [ ] Counterexamples are in your language's syntax, not CBMC internals
- [ ] Tests include negative cases (verification fails when expected)

---

## Maintenance Considerations

### Track language evolution

Languages change. Keep your frontend in sync with the language spec:
- Subscribe to language release notes
- Test against a recent compiler version
- Document which language version your frontend targets

### Minimize external dependencies

Every dependency is a maintenance burden. Prefer:
- Parser subprocesses over linked libraries (easier to update)
- Common TypeScript/JavaScript/Python versions (4-5 year window)
- Standard C++17 features (works with any recent compiler)

### Keep frontend code separate

Don't let frontend details leak into core CBMC. Use the `languaget`
abstract interface strictly. This makes it easier to:
- Add new frontends
- Refactor CBMC internals
- Maintain multiple language versions side-by-side

### Cross-reference against the language spec

Use inline comments like `// ES2024 sec-8.5.1` to tie implementation
details to the spec. Crucial for maintainers who need to reason about
correctness.

### Document trade-offs

Maintainers will wonder why you made certain decisions. Document:
- Why you chose X instead of Y
- Which features are unsupported and why
- How to extend the frontend to support more

---

## Summary Checklist for New Frontends

Minimum viable frontend:
- [ ] Directory structure created
- [ ] `languaget` implemented (parse, typecheck, id, extensions, show_parse)
- [ ] Parser working (built-in or external)
- [ ] Converter for basic expressions (literals, identifiers, binary ops)
- [ ] Converter for basic statements (var decl, assign, if/while, return)
- [ ] Function declarations handled
- [ ] `__CPROVER_start` entry point created
- [ ] Source locations tagged
- [ ] `from_expr` / `from_type` for counterexamples
- [ ] 10+ regression tests (including negative cases)

Production-ready frontend:
- [ ] All common language features supported
- [ ] Verification primitives (`__CPROVER_assume`, `assert`, etc.)
- [ ] Unit tests for utility functions
- [ ] Documentation (semantic compliance review, verification guide,
      benchmark suite)
- [ ] Cross-references to language spec throughout code
- [ ] Performance profiled and documented
- [ ] Integration tested on real-world code
- [ ] Known limitations documented
- [ ] **Per-spec soundness review completed for each major built-in
      subsystem** (expect 2–10 real bugs per review on a
      "works-for-common-cases" frontend; see Testing Strategy §)
- [ ] **Symbolic-input second pass completed** for each reviewed
      subsystem (catches methods that are sound for constants but
      fall through to nondet for symbolic inputs)
- [ ] **nondet_X()` primitives bounded** (length/range assumptions
      emitted so property-based tests are sound)
- [ ] **Regression suite mutation-tested** (verifies tests actually
      catch bugs they claim to; see Common Pitfalls §12)
- [ ] Per-subsystem review docs published (e.g. `string-soundness-review.md`)

The TypeScript frontend took ~500 tests and ~177 commits to reach
production-ready state; the Python frontend took a similar amount of
work. Budget accordingly.
