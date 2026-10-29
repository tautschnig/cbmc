# Go Front-End for CBMC — Design and Implementation Plan

## 1. Goals

Verify Go programs using CBMC's bounded model checking. Follow the
semantics defined by the Go language specification. Produce correct
verification results — soundness over completeness.

This document will grow as the design is settled. Its first decision
is which normative reference governs the frontend's semantics.

## 2. Language References

Go has no ISO, ECMA, or other external standard. The normative
definition of the language is the specification maintained by the Go
project, versioned together with each Go release. The `gc` toolchain and
`go/types` package are the reference implementation. Where the
specification is silent or says that behaviour is implementation-defined,
we follow `gc` on 64-bit targets and record the choice here.

### 2.1 Primary: The Go Programming Language Specification

- Version: "Language version go1.27 (May 26, 2026)"
- Pinned at tag `go1.27.3` (commit `2aae1000ea1d`), file
  `doc/go_spec.html`
- Online: https://go.dev/ref/spec
- Pinned source:
  https://raw.githubusercontent.com/golang/go/go1.27.3/doc/go_spec.html
- Local copy: `~/go-spec/go_spec.html`
  (sha256 `a249c493e0ad58ccfeac8081426d5b0e77f92b9761b95548f85c9e4d23d0a753`)
- Section index: `~/go-spec/go_spec_sections.txt` (159 anchored sections)

This specification governs syntax, the type system (including generics),
constants, expressions, statements, built-in functions, packages,
initialisation, and run-time panics.

**Citation format.** In code comments and documentation, cite the HTML
anchor id: `Go1.27 §Integer_overflow`, `Go1.27 §For_range`. Anchors are
stable across releases and resolve as `https://go.dev/ref/spec#<anchor>`.
This mirrors the `ES2024 sec-…` convention used by the TypeScript
frontend, and allows a coverage audit against the section index.

### 2.2 Secondary: The Go Memory Model

- Version: "Version of June 6, 2022" (unchanged through go1.27.3)
- Online: https://go.dev/ref/mem
- Local copy: `~/go-spec/go_mem.html`
  (sha256 `a7372b4f36c9525ecc7531bbdbb9e53746ae3f58367a25fe8084d5a35bb8ee7c`)
- Section index: `~/go-spec/go_mem_sections.txt`

This governs goroutines, channels, `sync`, and data races. It becomes
relevant once concurrency is supported. Cite as `GoMem §<anchor>`.

### 2.3 Tertiary: standard-library documentation

Unlike ECMA-262, which specifies built-in objects such as `String` and
`Array`, the Go specification does not cover the standard library (`fmt`,
`strings`, `errors`, `sort`, `maps`, `slices`, …). For library behaviour,
the reference is the package documentation and source at the same tag
(`src/<pkg>/` at `go1.27.3`, rendered at https://pkg.go.dev/std). This
parallels the Python frontend's split between the Language Reference and
the Library Reference.

## 3. Specification points with direct impact on verification

These items fall out of the reference choice and will constrain the
design. Each will receive a regression test.

| Spec section | Behaviour | Consequence for CBMC |
|---|---|---|
| `§Numeric_types` | `int`, `uint`, `uintptr` are 32 or 64 bits ("implementation-specific sizes") | `config.go.int_width` / `uintptr_width`, derived (as gc does) from `config.ansi_c.pointer_width`. These are auto-configured from the host and follow `--arch`, `--32`, `--64` (commit `f494777325`). |
| `§Integer_overflow` | Signed and unsigned integer overflow wraps (two's complement). It is **not** an error. | Do not enable C-style `--signed-overflow-check` semantics by default. Overflow is defined behaviour in Go. |
| `§Arithmetic_operators` | Integer division or remainder by zero is a run-time panic. Float division by zero follows IEEE 754. | Emit a panic assertion for integer division only. |
| `§Run_time_panics` | Out-of-range indexing, nil dereference, failed type assertion, and similar operations panic. | Model panics as assertion failures. `recover` handling comes later (`§Handling_panics`). |
| `§For_range` | Map iteration order is not specified. | Model the order as nondeterministic, never as a fixed order. |
| `§Select_statements` | Among ready cases, the choice is uniform pseudo-random. | Model the choice as nondeterministic. |
| `§Order_of_evaluation` | Operand order is only partially specified. | Must not assume a specific order where the spec leaves it open. |
| `§Constants` | Untyped constants are exact, arbitrary-precision values. | Evaluate constant expressions exactly at conversion time, as `go/types` does. |

## 4. Parsing and type checking: decision

**Decision: start with an external helper built on `go/parser` and
`go/types`, while retaining the native parser (§4.3).** The helper is a
small compiled Go program that type-checks the package and emits the
syntax tree as JSON annotated with type-checker results. This is Approach
1 of `building-a-new-frontend.md`. The C++ side loads that JSON into its
in-memory Go AST and never redoes name resolution, constant evaluation,
or type inference. We decided after spiking all three approaches. The
spikes live in `scripts/go-frontend-spikes/`; its `README.md` describes
how to reproduce the measurements.

### 4.1 Data

| | Spike A: external (`go/types`) | Spike B: native C++ | Library (gofrontend) |
|---|---|---|---|
| Parser | `go/parser` (reference) | 1,242 LOC C++. Parses 6,829/6,829 files (go1.27.3 stdlib plus corpus, 90 MB, ~6.5 s). Identical to `go/parser` on 21 node kinds in every file. Rejects 12/12 invalid inputs. | 70k LOC C++ |
| Type checker | `go/types` (reference), via a 200-LOC helper | Not attempted; see 4.2 | Present, but no type parameters and library at go1.18 |
| Resolution, generics, constants | All provided: inferred type arguments (`Sum` → `[int]`), promoted-method embedding paths and implicit dereferences, exact constants (`1<<100 >> 98` = 4) | Must be implemented | — |
| Latency per package | 0.18 s warm, 4.5 s once on a cold build cache (`gc` importer); 0.53 s (`source` importer) | n/a | — |
| Language evolution | Follows each Go release for free | Manual, every six months | Stalled at go1.18 |
| Runtime dependency | Go toolchain (`go1.27.x`) | None | None |

### 4.2 Why the native route is deferred

The native *parser* is clearly feasible. It reached full agreement with
`go/parser` after a handful of fixes, which are documented together with
its remaining gaps in `scripts/go-frontend-spikes/native/README.md`.
Type checking dominates the cost, however, and it is needed even for
trivial programs:

- `go/types` is 15.8k LOC of Go, plus `go/constant` (1.1k) and
  `math/big` (6.5k) for exact untyped constants. Generics alone (type
  inference, unification, instantiation, type sets) are 1.9k LOC. For
  comparison, CBMC's own C and C++ type checkers are 9.5k and 11.1k LOC.
  A faithful C++ port is estimated at 15–25k LOC.
- Every Go program imports the standard library, so the checker must
  also process its source. Importing `errors` pulls in 56k LOC across
  33 packages; `fmt` pulls in 101k LOC across 59 packages. Even choosing
  which files to compile requires Go's build-constraint logic: only
  587 of the 1,423 files in `fmt`'s dependency directories apply on
  linux/amd64. That source also contains 118 generic declarations,
  3,208 `unsafe` uses, 556 `//go:linkname` directives, and 275
  assembly-backed functions with no Go body.
- The language changes every six months (for example range-over-func
  iterators in go1.23 and generic type aliases in go1.24). With
  `go/types`, each release reaches us automatically.

The external route needs a Go toolchain at verification time. That is a
reasonable requirement for people verifying Go code, and it parallels the
TypeScript frontend's dependency on Node.js. The frontend reports a
missing helper clearly (§5); building the helper requires Go 1.27. The
JSON schema decouples the two halves, so a native type checker could
replace the helper later without changing the converter.

### 4.3 The native route is retained

The native parser stays in-tree as a basis for a possible native
front-end. `scripts/go-frontend-spikes/check_native.sh` checks it against
`go/parser` on the corpus and the whole stdlib, and checks that invalid
inputs are rejected. Run it whenever the parser changes or the pinned Go
version moves. `native/README.md` records the ambiguities and how they are
resolved, the bugs the structural oracle found, the known gaps, and a
resume plan. That plan includes a hybrid option: a native checker for
user code, with stdlib signatures pre-extracted by `go/types`, which
would remove the Go toolchain from the verification-time path.

To keep that route cheap, the C++ converter must consume an **in-memory
Go AST**, not the helper's JSON directly. The helper's JSON is loaded into
that AST; a native parser and checker would produce the same AST. That
AST is `src/go/go_parse_tree.h` (§5).

### 4.4 Lessons applied from the TypeScript frontend

- **Ship a compiled helper, not an inline script.** TypeScript embeds a
  JavaScript program in a C++ string literal. Instead, the Go helper
  lives as ordinary Go source in `src/go/helper/` and is built by CMake
  when `go` is available.
- **No daemon needed.** TypeScript needed `ts_ast_server` to amortise
  Node.js start-up. The Go helper runs in 0.18 s, so we use one
  subprocess per verification run.
- **Fail closed on type errors.** The helper exits non-zero, and CBMC
  reports a parse error, instead of verifying a partially-typed program.

### 4.5 Orthogonal: standard-library bodies

Whichever parser we use, CBMC cannot symbolically execute the real
runtime: `unsafe`, assembly, and `//go:linkname` are pervasive. As with
the C library models in `src/ansi-c/library/` and the Java models
library, we will need Go models for the stdlib functions programs use.
The helper supplies the stdlib *signatures*; the models supply the
*bodies*.

## 5. Status: first end-to-end slice

Steps 1 and 2 of the original plan are done. The code is in `src/go/`,
the tests are in `regression/go/`.

| Part | Files | Role |
|---|---|---|
| Helper | `helper/main.go`, `helper/cprover/` | `go/parser` + `go/types` → typed JSON (schema version 1). Type-switch bindings (`_impl`, from `types.Info.Implicits`), receiver-qualified method ids and file-qualified positions are emitted. |
| Go AST | `go_parse_tree.{h,cpp}` | In-memory `irept` tree loaded from the JSON. The converter reads only this tree (§4.3). |
| Types | `go_types.{h,cpp}` | `go/types` type table → `typet`. `int`/`uint`/`uintptr` come from `config.go`. |
| Converter | `go_convert.{h,cpp}` | Go AST → symbol table and code. Constructs outside the slice are reported as errors, never dropped. |
| Language | `go_language.{h,cpp}`, `expr2go.{h,cpp}` | `languaget` for `.go` files: runs the helper, sets the target GOARCH from `--arch`/`--32`/`--64`, builds the entry point. |

The slice supports:

- Types: integer and boolean types; untyped constants with their final
  types from `go/types`, folded exactly.
- Declarations: package-level variables initialised in
  `types.Info.InitOrder`, then `init` functions in source order
  (`§Package_initialization`), then `main.main` (`§Program_execution`)
  or the `--function` target with nondeterministic arguments.
  Functions with at most one result, including named results.
- Statements: `if`, `for` (all three forms), unlabelled `break` and
  `continue`, `return`, assignments (two-phase, `op=`, `++`/`--`, `:=`,
  blank identifier).
- Expressions: integer unary and binary operators with Go's wrap-around
  (`§Integer_overflow`), comparisons, `&&`/`||` short-circuiting,
  conversions between integer types, `min`/`max`, `panic`, `print`.
- Run-time panics, emitted by the converter as an assertion followed by
  an assumption because `goto_check_c` skips non-C modes. They cover
  integer division or remainder by zero (`division-by-zero`), negative
  signed shift counts (`negative-shift`), and `panic` (`panic`).
  CBMC's own semantics already match Go for `MinInt / -1`,
  `MinInt % -1`, and oversized shift counts.
- Verification primitives from package `cprover`: `Assert`, `Assume`,
  `Nondet[T]`.

Errors are reported as follows:

- Syntax and type errors from the helper are printed as
  `file:line:col: message` and end in `PARSING ERROR`.
- Unsupported constructs end in `CONVERSION ERROR`.
- If the helper is missing, CBMC says where it looks for it.
  The lookup order is `$CBMC_GO_HELPER`, then next to `cbmc`, then `PATH`.

Build and test wiring:

- CMake builds `cbmc-go-helper` when it finds `go`.
- With the Makefiles, build the helper with `make -C src/go helper`.
- `regression/go` runs only when the helper exists.
- Building the helper needs Go 1.27 (`go.mod`, `GOTOOLCHAIN=local`).
- At verification time, programs that import only `cprover` need no Go
  toolchain. Other imports go through the gc importer, which runs
  `go list -export` and so needs the toolchain.

Known limitations of the slice:

- **One file per package.** `cbmc` runs the helper on each `.go` file
  separately, so a package spread over several files is not yet
  supported. The helper itself accepts several files.
- **Single package.** Only the package being verified plus `cprover`.
  Calls into other packages, and uses of their variables, are reported
  as unsupported.
- The converter does not yet use `_impl`, because there are no type
  switches yet.
- Not yet supported: labelled statements, `goto`, `switch`, `select`,
  `defer`, `recover`, multiple results, strings, floats, complex
  numbers, pointers, arrays, slices, maps, structs, methods, interfaces,
  closures, generics beyond `cprover.Nondet`, goroutines, and channels.

The Makefile build of `cbmc` does not link the Python front-end that
`cbmc_languages.cpp` registers (by inspection: no Makefile builds
`src/python`). This comes from the not-yet-upstream Python front-end
work this branch is based on, predates the Go work, and is not fixed
here.

## 6. Feature phasing

Each phase lists the specification sections it implements. A phase is
done when every construct in it is converted or explicitly reported, and
each behaviour the spec defines has a regression test citing its
section.

1. **Control flow and functions.** `§Labeled_statements`, labelled
   `§Break_statements`/`§Continue_statements`, `§Goto_statements`,
   `§Switch_statements` (expression switches, `fallthrough`), multiple
   results and `§Return_statements` with several values, `§Calls` with
   multi-valued arguments, `§Function_literals` without captures.
2. **Composite value types.** `§Array_types`, `§Struct_types` (including
   embedded fields and promotion, using the helper's `_sel` paths),
   `§Pointer_types` with nil-dereference panics, `§Address_operators`,
   `§Composite_literals`, `§Comparison_operators` for arrays and structs,
   index-out-of-range panics (`§Index_expressions`).
3. **Strings, slices and maps.** `§String_types` (immutable byte
   sequences, `len`, indexing, `§Slice_expressions`, `+`, comparisons,
   `§For_range` over strings with UTF-8 decoding), `§Slice_types` with
   `make`, `append` and `copy` (`§Appending_and_copying_slices`),
   `§Map_types` with nondeterministic iteration order, `delete`, `clear`.
4. **Floating point.** `float32`/`float64` per IEEE 754
   (`§Arithmetic_operators`), numeric conversions (`§Conversions`), and
   `complex64`/`complex128`.
5. **Methods and interfaces.** `§Method_declarations`, method values and
   expressions, `§Interface_types` with dynamic dispatch,
   `§Type_assertions` and `§Type_switches` (using `_impl`), the `error`
   interface.
6. **Generics.** `§Type_parameter_declarations` and instantiation
   (`_inst`), by monomorphising each instantiation `go/types` reports.
7. **Panics and deferral.** `§Defer_statements`, `§Handling_panics`
   (`recover`), so that a run-time panic becomes an unwinding path
   instead of an assumption.
8. **Packages and the standard library.** Several files per package,
   imported user packages, and Go models of the stdlib functions programs
   use (§4.5).
9. **Concurrency.** `§Go_statements`, `§Channel_types`,
   `§Send_statements`, `§Receive_operator`, `§Select_statements` with a
   nondeterministic choice, and the Go Memory Model for data races.

Phases 1–3 cover most small verification harnesses. Phase 7 changes how
panics are encoded, so the converter keeps all panic checks behind one
helper (`go_convertert::guard_panic` and friends) to make that change
local.
