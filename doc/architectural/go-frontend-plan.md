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
| `§Numeric_types` | `int`, `uint`, `uintptr` are 32 or 64 bits ("implementation-specific sizes") | Fix 64 bits, matching `gc` on amd64/arm64. Consider a 32-bit option later. |
| `§Integer_overflow` | Signed and unsigned integer overflow wraps (two's complement). It is **not** an error. | Do not enable C-style `--signed-overflow-check` semantics by default. Overflow is defined behaviour in Go. |
| `§Arithmetic_operators` | Integer division or remainder by zero is a run-time panic. Float division by zero follows IEEE 754. | Emit a panic assertion for integer division only. |
| `§Run_time_panics` | Out-of-range indexing, nil dereference, failed type assertion, and similar operations panic. | Model panics as assertion failures. `recover` handling comes later (`§Handling_panics`). |
| `§For_range` | Map iteration order is not specified. | Model the order as nondeterministic, never as a fixed order. |
| `§Select_statements` | Among ready cases, the choice is uniform pseudo-random. | Model the choice as nondeterministic. |
| `§Order_of_evaluation` | Operand order is only partially specified. | Must not assume a specific order where the spec leaves it open. |
| `§Constants` | Untyped constants are exact, arbitrary-precision values. | Evaluate constant expressions exactly at conversion time, as `go/types` does. |

## 4. Next steps

1. Choose a parsing and type-checking approach. The leading candidate is
   an external helper built on `go/parser` and `go/types`, emitting typed
   JSON, analogous to the TypeScript frontend's use of the TypeScript
   Compiler API. A Go toolchain (`go1.27.x`) is required, and is not yet
   installed on the development host.
2. Draft a feature-phasing plan (types → expressions → statements →
   functions → slices, maps, and strings → structs, methods, and
   interfaces → generics → panics and `defer`/`recover` → goroutines and
   channels).
