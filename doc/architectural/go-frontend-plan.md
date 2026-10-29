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

**Decision: use an external helper built on `go/parser` and `go/types`.**
It is a small compiled Go program that type-checks the package and emits
the syntax tree as JSON annotated with type-checker results. This is
Approach 1 of `building-a-new-frontend.md`. The C++ side consumes the
JSON and never redoes name resolution, constant evaluation, or type
inference. We decided after spiking all three approaches. The spikes live
in `scripts/go-frontend-spikes/`; its `README.md` describes how to
reproduce the measurements.

### 4.1 Data

| | Spike A: external (`go/types`) | Spike B: native C++ | Library (gofrontend) |
|---|---|---|---|
| Parser | `go/parser` (reference) | 1,207 LOC C++. Parses 6,827/6,827 go1.27.3 stdlib files (90 MB, 6.2 s). Identical to `go/parser` on 18 node kinds in every file. Rejects 8/8 invalid inputs. | 70k LOC C++ |
| Type checker | `go/types` (reference), via a 200-LOC helper | Not attempted; see 4.2 | Present, but no type parameters and library at go1.18 |
| Resolution, generics, constants | All provided: inferred type arguments (`Sum` → `[int]`), promoted-method embedding paths and implicit dereferences, exact constants (`1<<100 >> 98` = 4) | Must be implemented | — |
| Latency per package | 0.18 s warm, 4.5 s once on a cold build cache (`gc` importer); 0.53 s (`source` importer) | n/a | — |
| Language evolution | Follows each Go release for free | Manual, every six months | Stalled at go1.18 |
| Runtime dependency | Go toolchain (`go1.27.x`) | None | None |

### 4.2 Why the native route was rejected

The native *parser* is clearly feasible: one pass and three small fixes
gave full agreement with `go/parser`. Type checking dominates the cost,
however, and it is needed even for trivial programs:

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
TypeScript frontend's dependency on Node.js. The frontend will detect a
missing or too-old `go` and report it clearly. The JSON schema decouples
the two halves, so a native type checker could replace the helper later
without changing the converter.

### 4.3 Lessons applied from the TypeScript frontend

- **Ship a compiled helper, not an inline script.** TypeScript embeds a
  JavaScript program in a C++ string literal. Instead, the Go helper
  will live as ordinary Go source under `src/go/` and be built by CMake
  when `go` is available.
- **No daemon needed.** TypeScript needed `ts_ast_server` to amortise
  Node.js start-up. The Go helper runs in 0.18 s, so we use one
  subprocess per verification run.
- **Fail closed on type errors.** The helper exits non-zero, and CBMC
  reports a parse error, instead of verifying a partially-typed program.

### 4.4 Orthogonal: standard-library bodies

Whichever parser we use, CBMC cannot symbolically execute the real
runtime: `unsafe`, assembly, and `//go:linkname` are pervasive. As with
the C library models in `src/ansi-c/library/` and the Java models
library, we will need Go models for the stdlib functions programs use.
The helper supplies the stdlib *signatures*; the models supply the
*bodies*.

## 5. Next steps

1. Turn the helper into `src/go/`'s JSON emitter. Settle the schema,
   including `types.Info.Implicits` (type-switch bindings),
   receiver-qualified method ids, and constants on declarations.
2. Add the `languaget` skeleton and `go2goto` conversion for a first
   vertical slice: `package main`, functions, integer arithmetic with
   wrap-around, `if`/`for`, and assertions.
3. Draft the feature-phasing plan (types → expressions → statements →
   functions → slices, maps, and strings → structs, methods, and
   interfaces → generics → panics and `defer`/`recover` → goroutines and
   channels).
