# CBMC Frontend Architecture

This document describes how CBMC handles programming languages through
its frontend architecture. It covers the abstract language interface,
the conversion pipeline from source code to CBMC's internal
representation, and the trade-offs in common frontend designs.

For a guide to **building a new frontend**, see the companion document
`building-a-new-frontend.md`.

## Table of Contents

1. [Overview](#overview)
2. [The Language Interface](#the-language-interface)
3. [The Conversion Pipeline](#the-conversion-pipeline)
4. [Central Data Structures](#central-data-structures)
5. [Existing Frontends](#existing-frontends)
6. [Design Trade-offs](#design-trade-offs)
7. [Common Patterns](#common-patterns)

---

## Overview

CBMC performs verification at the level of **GOTO programs** — a
low-level, language-agnostic control-flow graph. Frontends translate
source code from their language into GOTO programs via a shared type
system (`irept` and its subclasses `exprt`, `typet`, `codet`).

A frontend's job is to:
1. **Parse** source code into an abstract syntax tree (AST)
2. **Type-check** the AST, resolving identifiers, computing types
3. **Convert** the AST into a CBMC symbol table of GOTO functions

CBMC then performs bounded model checking on the GOTO program via:
- Instrumentation passes (bounds checking, pointer safety, etc.)
- Symbolic execution with loop unwinding
- SAT/SMT encoding and solving

The frontend is the only part that knows about the source language.
Everything downstream works on the language-neutral GOTO representation.

---

## The Language Interface

All frontends implement the `languaget` abstract interface defined in
`src/langapi/language.h`. Key methods:

### Required methods

```cpp
class languaget {
public:
  // Parse source file into frontend's internal AST
  virtual bool parse(
    std::istream &instream,
    const std::string &path,
    message_handlert &) = 0;

  // Produce the GOTO symbol table from the parsed AST
  virtual bool typecheck(
    symbol_table_baset &symbol_table,
    const std::string &module,
    message_handlert &) = 0;

  // Language identification
  virtual std::string id() const = 0;
  virtual std::string description() const = 0;
  virtual std::set<std::string> extensions() const = 0;

  // Show the parsed AST for debugging
  virtual void show_parse(std::ostream &, message_handlert &) = 0;

  // Format an expression or type in source syntax (for counterexamples)
  virtual bool from_expr(
    const exprt &, std::string &code, const namespacet &);
  virtual bool from_type(
    const typet &, std::string &code, const namespacet &);
};
```

### Optional methods

```cpp
// Set language-specific command-line options
virtual void set_language_options(const optionst &, message_handlert &);

// Generate language-specific support functions (e.g., __CPROVER_start)
virtual bool generate_support_functions(
  symbol_table_baset &, message_handlert &);

// Lazy method loading (for large codebases with many unused functions)
virtual void methods_provided(std::unordered_set<irep_idt> &) const;
virtual void convert_lazy_method(
  const irep_idt &, symbol_table_baset &, message_handlert &);

// Dependency tracking for separate compilation
virtual void dependencies(
  const std::string &module, std::set<std::string> &modules);

// Preprocessing (C macro expansion, etc.)
virtual bool preprocess(
  std::istream &, const std::string &, std::ostream &, message_handlert &);
```

### Registration

Each frontend provides a factory function:
```cpp
std::unique_ptr<languaget> new_mylanguage_language();
```

Registered in `src/langapi/mode.cpp` and invoked automatically based
on file extension.

---

## The Conversion Pipeline

### Stage 1: Parsing

Input: source text (stream or file path)
Output: language-specific AST (typically stored in the frontend object)

The frontend chooses its parsing strategy. Common approaches:

- **Hand-written recursive descent** (ansi-c, cpp): Maximum control,
  but labor-intensive. Handles language quirks directly.
- **Generated parser via yacc/bison** (ansi-c uses flex/bison): Good
  for clean grammars, standard tooling.
- **External parser via subprocess** (python, typescript): Delegate
  to the language's reference implementation. Easiest to build, but
  introduces a process boundary.
- **Library integration** (java_bytecode uses miniz for JAR): Link
  against an existing parser library.

The AST representation can be either:
- **Custom AST types** (ansi-c `ansi_c_parse_treet`)
- **Generic `irept` tree** (cpp uses `cpp_parse_treet`)
- **JSON from external tool** (python, typescript)

### Stage 2: Type-checking and Conversion

Input: frontend's AST
Output: populated `symbol_table_baset`

This is typically the biggest part of the frontend. It involves:

1. **Symbol table population**: Create `symbolt` entries for every
   function, variable, type, and constant.
2. **Type resolution**: Convert language types to CBMC types (`typet`).
3. **Expression conversion**: Convert AST expressions to `exprt`.
4. **Statement conversion**: Convert AST statements to `codet`.
5. **Function body conversion**: Each function's body becomes a
   `code_blockt` stored in the symbol's `value`.
6. **Instrumentation**: Add language-specific checks (null checks,
   array bounds, type casts).

### Stage 3: GOTO Conversion

After `typecheck`, CBMC's `goto_convert` module (language-independent)
transforms the symbol table into GOTO programs with labels and gotos.
This is not the frontend's concern.

---

## Central Data Structures

### `irept` — The Base

Every expression, type, and statement in CBMC derives from `irept`:
```cpp
class irept {
  irep_idt id;                  // what kind of thing
  std::map<irep_idt, irept> named_sub;
  std::vector<irept> sub;
};
```

### `typet` — Types

Examples the frontend must produce:
- `signedbv_typet{N}` — N-bit signed integer
- `unsignedbv_typet{N}` — N-bit unsigned integer
- `floatbv_typet{spec}` — IEEE 754 float
- `bool_typet{}` — boolean
- `pointer_typet{base, width}` — pointer to base type
- `array_typet{elem, size}` — fixed-size array
- `struct_typet{components}` — struct/record
- `code_typet{params, return_type}` — function type
- `empty_typet{}` — void/unit

### `exprt` — Expressions

Examples:
- `constant_exprt{val, type}` — literal
- `symbol_exprt{id, type}` — variable/function reference
- `plus_exprt{l, r}`, `minus_exprt`, `mult_exprt`, `div_exprt` — arithmetic
- `equal_exprt`, `notequal_exprt`, `binary_relation_exprt{l, op, r}`
- `and_exprt`, `or_exprt`, `not_exprt` — logical
- `if_exprt{cond, then, else}` — ternary
- `member_exprt{obj, name, type}` — struct field access
- `index_exprt{arr, idx}` — array indexing
- `address_of_exprt{e}`, `dereference_exprt{p}` — pointer ops
- `typecast_exprt{e, new_type}` — type conversion
- `side_effect_expr_function_callt{callee, args, ret_type, loc}`

### `codet` — Statements

Examples:
- `code_blockt{stmts}` — sequence
- `code_frontend_assignt{lhs, rhs}` — assignment
- `code_assumet{cond}`, `code_assertt{cond}` — assumptions/assertions
- `code_ifthenelset{cond, then, else}` — conditional
- `code_whilet{cond, body}` — while loop
- `code_forwhile`, `code_dowhile` — other loop shapes
- `code_frontend_returnt{value}` — return
- `code_breakt{}`, `code_continuet{}` — loop control
- `code_gotot{label}` — direct jump (for complex control flow)
- `code_labelt{label, body}` — labeled statement
- `code_expressiont{e}` — expression statement (side effects)
- `code_skipt{}` — no-op

### `symbolt` — Symbol Table Entries

```cpp
class symbolt {
  irep_idt name;       // fully qualified: "my_lang::foo"
  irep_idt base_name;  // "foo"
  irep_idt mode;       // "C", "cpp", "python", "typescript"
  typet type;          // function type, variable type, etc.
  exprt value;         // function body (code_blockt) OR initial value
  source_locationt location;
  bool is_lvalue;      // can be assigned to
  bool is_static_lifetime;
  bool is_parameter;
  bool is_state_var;
  // ... more flags
};
```

Fully qualified names typically follow the pattern
`<language_mode>::<module>::<symbol>` (e.g., `typescript::MyClass::foo`).

---

## Existing Frontends

### ansi-c (`src/ansi-c/`)
- **Lines**: ~30,000+
- **Parser**: flex/bison (hand-coded where needed)
- **Approach**: Direct conversion from C AST to CBMC types
- **Strengths**: Mature, handles preprocessor, supports C89/C99/C11/C17/C23
- **Complexity**: Very high — C has many edge cases (flexible arrays,
  bit-fields, designated initializers, etc.)

### cpp (`src/cpp/`)
- **Lines**: ~25,000+
- **Parser**: Hand-written C++ parser
- **Approach**: Depends on ansi-c for C compatibility; extends with
  classes, templates, overloading
- **Strengths**: Handles much of C++17
- **Complexity**: Highest — C++ has notoriously complex semantics

### java_bytecode (`jbmc/src/java_bytecode/`)
- **Lines**: ~20,000+
- **Parser**: Parses .class files directly (JVM bytecode format)
- **Approach**: Bytecode → JVM operand stack simulation → GOTO
- **Strengths**: Handles Java's reference semantics, generic erasure,
  exception handling
- **Complexity**: High — full JVM instruction set

### python (`src/python/`)
- **Lines**: ~10,000+ (after recent work on tautschnig/py)
- **Parser**: External (Python `ast` module via subprocess)
- **Approach**: JSON AST from Python → direct conversion
- **Strengths**: Handles dynamic typing via tagged unions, string
  solver integration, bounded models (list, dict, set)
- **Complexity**: Medium — Python's flexibility requires many runtime
  checks and overapproximations

### typescript (`src/typescript/`)
- **Lines**: ~7,000+
- **Parser**: External (TypeScript Compiler API via Node.js subprocess)
- **Approach**: JSON AST → monomorphized conversion
- **Strengths**: Full TypeScript type system, generics via monomorphization,
  discriminated unions, Map/Set/Promise support
- **Complexity**: Medium — leverages TypeScript compiler for parsing
  and type resolution

### Abstractions and Shared Utilities

- **langapi/**: Abstract interface, mode registry
- **util/**: Common types (`irept`, `exprt`, `typet`, `codet`, `symbolt`)
- **json/**, **xmllang/**: JSON/XML parsing for external tools
- **linking/**: Merges symbol tables across files

---

## Design Trade-offs

### Parser: Built-in vs External

| Aspect | Built-in (flex/bison) | External (subprocess) |
|--------|----------------------|----------------------|
| Initial development | Slow | Fast (reuse existing parser) |
| Language updates | Manual effort | Automatic (language updates via tool) |
| Runtime overhead | None | Process startup + JSON serialization |
| Dependencies | Just a parser generator | Requires language tooling installed |
| Integration | Tight, direct AST access | Loose, serialization boundary |
| Debugging | Standard C++ tools | Need to inspect JSON |
| Best for | Established, stable languages (C) | Rapidly evolving or complex languages (TypeScript, Python) |

**Rule of thumb**: If your language already has a fast, robust parser
with a clean AST API, use it. If not or if the language is simple,
build your own.

### Type System Modeling

**Strong static → strong static** (TypeScript → CBMC): straightforward
direct mapping.

**Dynamic → static** (Python → CBMC): requires runtime type tags,
tagged unions, or type inference. Each value needs to carry type
information so operations can dispatch correctly.

**Complex OO → flat struct** (C++, Java → CBMC): class hierarchies
become struct inheritance. Virtual dispatch becomes function pointer
tables or case analysis.

### Bounded vs Unbounded Structures

Dynamic languages have unbounded arrays, dicts, strings. CBMC needs
bounded structures for verification.

**Options:**
- **Fixed small bound** (e.g., `MAX_ARRAY_LENGTH = 16`): fast, but
  programs exceeding the bound give unsound results (CBMC can miss bugs).
- **Configurable bound** (`--max-list-length N`): user controls trade-off.
- **Symbolic length with bounded storage**: struct with `length` field
  and fixed-size data array. CBMC's array theory handles this well.

**Soundness consideration**: Always document the bound clearly. An
unsound overapproximation (e.g., nondet for out-of-bounds) is WORSE
than a sound one that reports "can't verify beyond N."

### Generics and Polymorphism

**Monomorphization** (specialize per type parameter): produces clean
type-safe code; each instantiation is a separate function/class. Used
by TypeScript frontend (`identity<number>` → `identity__number`).

**Type erasure** (treat generics as their bounds): simpler but loses
type information. Used by Java bytecode frontend (generics are erased
at compile time anyway).

**Union types with runtime tags**: for languages where the same code
path handles different types. Used by Python frontend.

### Eager vs Lazy Function Conversion

**Eager**: convert all function bodies upfront during `typecheck`.
Simple, but slow for large codebases with many unused functions.

**Lazy**: convert on-demand via `convert_lazy_method` callback. CBMC
requests function bodies only when needed during symbolic execution.
Much faster for large programs.

Java bytecode uses lazy loading; most other frontends use eager.

### Built-in Runtime Functions

Every language needs:
- **Nondet**: user-writable hook for modeling unknown inputs
  (`__CPROVER_assume`, `nondet_number`, `Math.random`)
- **Entry point**: `__CPROVER_start` for verification start
- **Library stubs**: minimal implementations of language standard library

Decide how much of the standard library to model:
- **Just core**: minimal effort, but programs using stdlib can't verify
- **Full model**: expensive to build, but programs verify out-of-the-box
- **Nondet stubs**: safe overapproximation, but may lose precision

### Soundness vs Completeness

CBMC is a **bounded** model checker — it verifies up to a bound, not
unconditionally. Within that bound, verification can be:

- **Sound**: if CBMC says "OK", there's no bug within the bound
- **Complete**: if there's a bug within the bound, CBMC finds it

Frontends choices affect soundness:
- **Nondet for unsupported features**: sound (overapproximation) but
  may report false positives
- **Silently skip unsupported features**: UNSOUND — might miss real bugs
- **Error out on unsupported features**: sound but blocks verification
  of programs that use any unsupported feature

**Recommendation**: prefer nondet + clear documentation over silent
skipping. Never trade soundness for performance without explicit
user opt-in.

---

## Common Patterns

### Pattern: String representation

**Approach 1: Pointer + refinement** (CBMC's native `refined_string_typet`)
- Struct `{length: int, content: char*}`
- Requires `--refine-strings` flag and string solver
- Supports unbounded string operations

**Approach 2: Inline array** (used by Python, TypeScript frontends)
- Struct `{length: int, data: char[MAX]}`
- No pointer indirection — direct character access
- Works with CBMC's array theory alone
- Bounded (programs exceeding MAX are unsound)

### Pattern: Object identity

Most languages have reference semantics (objects shared by reference).
CBMC struggles with this because structs are value-typed.

**Workaround**: Allocate objects on a "heap" (global struct array),
pass indices instead of pointers. Or use actual CBMC pointers with
`address_of` / `dereference`.

### Pattern: Closures

Capturing variables requires either:
1. **Extra parameters**: pass captured values as implicit args (works for
   read-only capture, breaks for shared mutable state)
2. **Environment struct**: bundle captured vars into a struct, pass
   pointer. Proper closure semantics but more complex.

TypeScript frontend uses (1) for simple cases and falls back to direct
symbol access via scope chain for shared mutable state.

### Pattern: Async/Await

Modeled synchronously by default (treat `await x` as `x`). Fine for
single-threaded programs without race conditions. True async requires
modeling the event loop with task queues and non-deterministic
scheduling (see `doc/async-ordering-plan.md`).

### Pattern: Dynamic features (eval, reflection, macros)

Generally unsupported in verification. Options:
- Reject programs using these features
- Treat as nondet (sound but imprecise)
- Require static approximation (if user provides the eval'd code)

### Pattern: Verification primitives

Every frontend should expose:
- `__CPROVER_assume(cond)` — constrain nondet values
- `__CPROVER_assert(cond, msg)` — custom assertion
- `__CPROVER_havoc_object(x)` — reset to nondet
- `__CPROVER_cover(cond)` — coverage goal
- `nondet_<type>()` — explicit nondet creation

These give users control over the verification context.

---

## References

- `src/langapi/language.h` — Abstract language interface
- `src/langapi/language_file.cpp` — Language registration and dispatch
- `src/util/std_expr.h` — Expression type hierarchy
- `src/util/std_code.h` — Statement type hierarchy
- `doc/architectural/cbmc-architecture.html` — High-level CBMC architecture
- `doc/architectural/folder-walkthrough.md` — Directory structure
- `AGENTS.md` — Developer guide (includes frontend patterns)
- `building-a-new-frontend.md` — Step-by-step guide (companion doc)
