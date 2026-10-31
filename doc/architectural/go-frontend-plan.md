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

## 5. Status

The first end-to-end slice and phases 1 to 8 of §6 are done. The code is in
`src/go/`, the tests are in `regression/go/`.

| Part | Files | Role |
|---|---|---|
| Helper | `helper/main.go`, `helper/packages.go`, `helper/cprover/`, `helper/runtime/`, `helper/models/` | `go/parser` + `go/types` → typed JSON (schema version 1). Type-switch bindings (`_impl`, from `types.Info.Implicits`), receiver-qualified method ids, file-qualified positions and the bytes of string constants (`_sb`) are emitted. Each type entry has a canonical id `tid` (the first entry of a `types.Identical` class), and each non-interface type its method set `mset` (name, method object, embedded-field path, whether the receiver is reached through a pointer, whether it is a pointer receiver, and the method-value type). Generic code is monomorphised here (see below), so the converter sees no type parameters. The runtime package `cprover/rt` (`helper/runtime/rt.go`) is type-checked and emitted with every program. The files given to it form the program's package; the user packages it imports are loaded from source, and models of standard-library packages (`helper/models/`) where the program imports those (see below). Each package has its initialisation order (`packages`, in the order in which the packages are initialised). |
| Go AST | `go_parse_tree.{h,cpp}` | In-memory `irept` tree loaded from the JSON. The converter reads only this tree (§4.3). |
| Types | `go_types.{h,cpp}` | `go/types` type table → `typet`. `int`/`uint`/`uintptr` come from `config.go`. Named struct types become struct tags (type symbols named like objects, e.g. `go::main.Node`, and `go::main.Pair[int,string]` for instances of generic types), so recursion through pointers is finite; other named types are their underlying type. Blank fields are kept under the names `_$N`. Booleans are stored in a byte (`c_bool`), so that they have a size (for heap variables and arrays); `as_condition` turns them into conditions. `float32`/`float64` are IEEE 754 `floatbv` types; complex numbers are structs `{real, imag}` marked `#go_complex` (CBMC's complex type is not lowered in function types). Strings, slices and maps: see below. Function values are structs `{code, env}` marked `#go_func_value` (see below); several results are returned as one struct marked `#go_tuple`, with components `$0`, `$1`, ... All interface types are one struct `{type int32, data *void}` marked `#go_interface` (see below). Each type records its canonical Go type id in the comment `#go_tid`. |
| Converter | `go_convert.{h,cpp}`, `go_builtins.cpp`, `go_maps.cpp`, `go_floats.cpp`, `go_interfaces.cpp`, `go_panics.cpp` | Go AST → symbol table and code. Constructs outside the slice are reported as errors, never dropped. Strings, slices and their built-ins are in `go_builtins.cpp`, maps in `go_maps.cpp`, floating-point and complex numbers in `go_floats.cpp`, methods and interfaces in `go_interfaces.cpp`, panics, `defer` and `recover` in `go_panics.cpp`. |
| Language | `go_language.{h,cpp}`, `expr2go.{h,cpp}` | `languaget` for `.go` files: runs the helper (on each file to check its syntax, then once on all of them), sets the target GOARCH from `--arch`/`--32`/`--64`, builds the entry point. |

Supported so far:

- Types: integer, floating-point, complex and boolean types, strings,
  pointers, arrays, slices, structs (including embedded fields), maps,
  function types and interface types; untyped constants
  with their final types from `go/types`, folded exactly.
- Declarations: package-level variables initialised in
  `types.Info.InitOrder`, then `init` functions in source order
  (`§Package_initialization`), then `main.main` (`§Program_execution`)
  or the `--function` target with nondeterministic arguments.
  Functions with any number of results, named or not (`return f()`,
  `x, y := f()`, `var x, y = f()` also at package level, `f(g())`).
- Statements: `if`, `for` (all three forms), `switch` with or without a
  tag, including `fallthrough` (as a chain of conditional jumps, which
  keeps Go's evaluation order of non-constant case expressions),
  labelled statements, `break` and `continue` with or without a label,
  `goto`, `return`, assignments (two-phase, with the operands on the
  left evaluated first and each target's checks performed at its
  assignment; `op=` and `++`/`--` evaluate the target once; `:=`, blank
  identifier), and `for range` over integers, arrays (copied) and
  pointers to arrays, with per-iteration variables.
- Function values: declared functions and function literals, including
  closures. A function value is a pair of a code pointer and an
  environment pointer; the code takes the environment as a hidden first
  parameter `$env`. Literals are lifted to functions named
  `<enclosing>$lit<N>`, and declared functions used as values get an
  adapter `<f>$value` that ignores the environment. A closure's
  environment is a new heap struct of pointers to the variables it
  captures. Calls through function values, and comparison with `nil`.
- Variables that outlive their function (`§Variables`): local variables
  whose address is taken (`&x`, `&x.f`, `&x[i]` for an array `x`) or
  that a function literal captures are allocated on the heap
  (`ALLOCATE`) at each execution of their declaration, as gc does after
  escape analysis; uses go through the pointer `x$ptr`. Loop variables
  of a `for` clause get a new variable per iteration (`§For_clause`,
  [Go 1.22]) when they live on the heap; otherwise the difference is not
  observable.
- Pointers and composite values: `&x`, `*p`, `new(T)` and `new(x)`,
  field selectors with implicit dereferences along embedded-field paths
  (`_sel` from the helper), indexing of arrays and pointers to arrays,
  struct and array composite literals (keys, promoted-field keys
  [Go 1.27], `[...]`, elided types and elided `&T`), `&T{...}`, `len` and
  `cap` of arrays, value semantics of struct and array assignment,
  equality of pointers, structs (ignoring blank fields) and arrays, and
  conversions between types with identical underlying types.
- Expressions: integer unary and binary operators with Go's wrap-around
  (`§Integer_overflow`), comparisons, `&&`/`||` short-circuiting,
  conversions between integer types, `min`/`max`, `panic`, `print`.
- Strings (`§String_types`) are `{data *uint8, len int}`: a pointer to
  the first byte and the length, with a null pointer for `""`. Constants
  are static arrays (one per distinct constant); concatenation,
  conversions and `[]byte`/string copies allocate a new array. Strings are
  immutable, so substrings share bytes. `len`, indexing, slicing,
  `+`/`+=`, `==`/`!=`/`<`/`<=`/`>`/`>=`, `range` over code points,
  conversions to and from `[]byte` and `[]rune`, and from integers.
  Equality, ordering and UTF-8 encoding and decoding are Go functions in
  the runtime package `cprover/rt`, converted with the program; they loop
  once per byte or rune, so they need loop bounds of at least the string
  length.
- Slices (`§Slice_types`) are `{data *E, len int, cap int}`, nil with a
  null data pointer; underlying arrays are dynamic objects allocated
  where they are created (`make`, slice literals, `append` growing,
  conversions), or existing arrays (slicing an array puts it on the
  heap). Indexing, simple and full slice expressions on arrays, pointers
  to arrays, slices and strings, `make`, `append` (also `s...` and
  `[]byte` with `string...`), `copy` (also from strings), `clear`,
  `range`, `len`, `cap`, and comparison with nil. When `append` must
  grow, the new capacity is nondeterministic, as the spec does not fix
  it. Element ranges are copied as by CBMC's `memcpy` model: with
  `__CPROVER_array_copy` into a new array of the exact length and
  `__CPROVER_array_replace`, without loops and independent of overlaps.
  Elements that contain pointers (strings, slices, pointers, function
  values) are copied one by one in a loop instead, because a byte-wise
  copy would lose what the pointers point to. Allocations go through
  `void *` with an element-sized `sizeof` annotation, as C's `malloc`,
  so that goto-symex derives the array type and gives it a size variable
  of its own.
- Maps (`§Map_types`) are pointers to a header `{len, cap, keys *K, vals
  *V}` on the heap (so map values share the map), nil as the null
  pointer. Lookups (also `v, ok := m[k]`), assignments (with `op=` and
  `++`), `delete`, `clear`, `len`, `make`, map literals and `range` search
  the keys linearly with Go's `==`, so they need loop bounds of at least
  the number of entries. `range` visits a snapshot of the keys in a
  nondeterministic order, skipping keys deleted before they are reached;
  entries added during the iteration are not visited, which the spec
  allows.
- Floating-point and complex numbers (`§Numeric_types`): constants are
  the IEEE 754 bits of their exact value rounded to the type, which the
  helper records (`_fb`, `_fbr`/`_fbi`). Operations round to nearest,
  ties to even. Division by zero gives infinities or NaN without a panic
  (`§Floating_point_operators` leaves this implementation-specific; gc
  does not panic). Comparisons follow IEEE 754 (`NaN != NaN`, `-0 ==
  +0`), also in struct and array equality and for map keys. Conversions
  truncate towards zero (float to integer) or round (to floating point);
  a float-to-integer conversion whose result is not representable,
  including NaN, gives an arbitrary value, as the spec makes it
  implementation-dependent. `min`/`max` follow the spec's table for -0,
  infinities and NaN. Complex numbers: `complex`, `real`, `imag`,
  arithmetic, conversions and `==`. Multiplication computes the parts as
  gc does, `(ac - bd)` and `(ad + bc)` in float64, rounded back for
  complex64; division is `rt.ComplexDiv`, which gives gc's results
  (Smith's algorithm with the C99 Annex G corrections; complex64 as
  complex128), checked against gc bit for bit on about 3.2 million
  quotients including zeros, infinities and NaNs. CBMC's floating-point
  passes read `__CPROVER_rounding_mode`, which the front-end declares
  (round to nearest).
- Methods (`§Method_declarations`) are functions `go::<pkg>.<T>.<M>`
  with the receiver as first parameter. Calls follow the helper's
  `_sel` path through embedded fields (with nil checks), and take `&x`
  for pointer receivers or `*x` for value receivers reached through a
  pointer. Calling a pointer-receiver method on an addressable variable
  puts the variable on the heap. Method values (`x.M`) are closures
  `<method>$bound<tid>` whose environment is a heap copy of the receiver,
  evaluated when the method value is; method expressions (`T.M`,
  `(*T).M`) are adapters `<method>$expr<tid>`.
- Interface values (`§Interface_types`) are `{type, data}`: the dynamic
  type's id (canonical `tid` + 1; 0 for nil) and a pointer to an
  immutable heap copy of the dynamic value. The dynamic types considered
  are those the program converts to an interface type, which is every
  type a value of interface type can have. Calls of interface methods go
  through generated dispatch functions `go::$method.<sig>.<Name>` that
  test the type id and call the method of each matching type; a nil
  interface value panics (`nil-dereference`). Embedded interfaces,
  `§Type_assertions` (also the comma-ok form, and to interface types via
  `go::$implements.<tid>`), `§Type_switches` (guard evaluated once, cases
  in order, implicit variables from `_impl`), `==` between interface
  values and with non-interface values (`go::$interface_equal`), and the
  predeclared `error` interface. The dispatch functions are filled in
  after all code is converted, when all dynamic types are known.
- Generics (`§Type_parameter_declarations`, `§Instantiations`), by
  monomorphisation in the helper. Generic declarations are not emitted;
  instead each instance of a generic function or method that the
  program uses becomes a function declaration of its own, named after
  its type arguments: `main.Max[int]`, `main.Pair[int,string].Swap`,
  and for generic methods [Go 1.27] `main.List[int].Apply[string]`.
  The helper encodes the generic declaration once per instance with
  every type parameter replaced by its type argument (in expression
  types, constants, which then get the IEEE 754 bits of the type
  argument, and object types), gives the objects declared in it ids of
  their own per instance (`<id>@<instance id>`), and resolves selectors
  and struct-literal keys again in the substituted types, so that
  methods of type arguments and fields of instantiated structs are
  found. Instances are found from `types.Info.Instances`, from the
  bodies of other instances, and from method sets (every method of an
  instantiated type, so that interface dispatch can call it). Type
  arguments are inferred by go/types; identical type arguments (e.g.,
  an alias) give the same instance. Instantiated types are ordinary
  types of their own (`types.Instantiate` with the type checker's
  context, so that identical instances are one type). `--function`
  takes an instance by its base name, e.g. `--function 'Max[int]'`.
- Variadic functions (`§Passing_arguments_to_..._parameters`): calls
  pass a new slice of the final arguments (nil if there are none), or
  `s...` unchanged; also for methods, function values and generic
  functions.
- Packages (`§Packages`, `§Import_declarations`). The `.go` files given
  to `cbmc` are the program's package, as for `go run a.go b.go`: syntax
  errors are reported per file (`PARSING ERROR`); then the helper
  type-checks all of them together (type errors end in `CONVERSION
  ERROR`, as for C), sorted by name as the go command presents them,
  which fixes the order of `init` functions. Packages of the main
  module (found through the `go.mod` enclosing the files) are loaded from
  their directories, with the files `go build` would select (build
  constraints, no tests; cgo is reported); packages of other modules
  are located with the go command. All of them are converted, with
  object ids qualified by the import path (e.g.
  `example.com/shapes/geom.Rect.Area`). Each package has its own
  initialisation function `go::<path>.$init`; `go::$init` calls them in
  the order of `§Package_initialization` (sorted by import path, each
  after the packages it imports), and the entry point calls it.
- Models of standard-library functions (§4.5), in
  `src/go/helper/models/<path>`: Go code converted with the program as
  package `cprover/std/<path>` when the program imports `<path>`. The
  real package (from the toolchain's export data) still provides the
  declarations, so programs type-check as with gc; a call of a function
  with a model of identical signature (for generic functions: of
  identical instance) denotes the model, and calls of other functions
  are reported. The models follow the standard library's code (BSD
  licence, attributed in each file) or its documented results, and were
  checked against the real functions on hundreds of thousands of random
  inputs each (strings with ASCII, multi-byte, invalid and space
  characters; floats including zeros, infinities, NaNs and boundary
  values; NaN payloads excepted). Modelled: `errors` (`New`, `Is`,
  `Unwrap`, `AsType`; not `As`, which needs reflection), `strings`
  (`Compare`, `Contains`, `ContainsRune`, `Count`, `Cut`, `CutPrefix`,
  `CutSuffix`, `Fields`, `HasPrefix`, `HasSuffix`, `Index`, `IndexByte`,
  `Join`, `LastIndex`, `Repeat`, `Split`, `TrimPrefix`, `TrimSpace`,
  `TrimSuffix`), `strconv` (`FormatBool`, `FormatInt`, `FormatUint`,
  `Itoa`), `unicode/utf8` (`AppendRune`, `DecodeRune`,
  `DecodeRuneInString`, `EncodeRune`, `RuneCount`, `RuneCountInString`,
  `RuneLen`, `Valid`, `ValidRune`, `ValidString`), `math` (`Abs`,
  `Ceil`, `Copysign`, `Float32bits`, `Float32frombits`, `Float64bits`,
  `Float64frombits`, `Floor`, `Inf`, `IsInf`, `IsNaN`, `Max`, `Min`,
  `Modf`, `NaN`, `Signbit`, `Trunc`), `slices` (`Contains`, `Equal`,
  `Index`, `Max`, `Min`, `Reverse`) and `fmt` (`Print`, `Printf`,
  `Println`: the output is not modelled; they call the `Error` or
  `String` method of each argument, recovering from panics in them, and
  their results are arbitrary). Constants of standard-library packages
  (e.g. `math.MaxInt64`) are folded by go/types as before. The runtime
  package provides intrinsics that the converter implements:
  `rt.Comparable` (whether a dynamic type is comparable, for
  `errors.Is`) and the IEEE 754 bit conversions (byte extraction).
- Run-time panics, emitted by the converter (`goto_check_c` skips non-C
  modes) as a jump to the end of the function after recording the panic
  (see below). They cover
  integer division or remainder by zero (`division-by-zero`), negative
  signed shift counts (`negative-shift`), dereferences of nil pointers
  and calls of nil function values (`nil-dereference`), non-constant
  array, slice and string indices out of range (`index-out-of-range`),
  slice bounds out of range (`slice-bounds-out-of-range`), invalid `make`
  sizes and slices growing beyond what `int` holds (`makeslice`),
  assignments to entries of nil maps (`nil-map`), failed type assertions
  (`type-assertion`), comparisons of interface values whose dynamic type
  is not comparable (`uncomparable`), and `panic` (`panic`).
  Checks also run in the runtime package (properties of
  `go::cprover/rt.*`).
  CBMC's own semantics already match Go for `MinInt / -1`,
  `MinInt % -1`, and oversized shift counts.
- Panicking, `defer` and `recover` (`§Handling_panics`,
  `§Defer_statements`). A panic sets the thread-local `go::$panicking`,
  records the panic site (`go::$panic_site`) and, if the program calls
  `recover`, the value (`go::$panic_value`), and jumps to the label
  `$unwind` at the end of the function. There the deferred calls run, in
  reverse order, and the function returns. After every call of a Go
  function, the caller jumps to its own `$unwind` if `go::$panicking` is
  set, so a panic unwinds up to the top-level function. The entry point
  calls `go::$check_panics` after `main.main` (which does not run if
  initialisation panicked); it has one assertion per panic site, with the
  site's location, property class and message, which fails if that panic
  ended the program. Property names are as before, e.g.
  `go::main.div.division-by-zero.1`, and a recovered panic is not a
  violation. The values of run-time panics are `cprover/rt.Error`
  values (an `error` with gc's message, e.g. "runtime error: integer
  divide by zero"). `defer f(args)` evaluates the function value and the
  arguments into a new environment on the heap and saves a function
  value of type `func()` calling it (`<function>$deferN`); a function
  whose defer statements each run at most once keeps one variable per
  statement, otherwise (a defer in a loop, or `goto`) a list on the heap.
  Return statements set the results (named, or a hidden `$tmp`) and jump
  to `$unwind`, so deferred functions can change named results.
  `recover` returns the panic value and stops panicking if the goroutine
  is panicking and the calling function was called directly by the
  deferral: the deferral sets `go::$defer_token`, which every Go function
  clears on entry after reading it.
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
- In CI, the Ubuntu 24.04 `make-gcc` and `cmake-gcc` jobs of
  `.github/workflows/pull-request-checks.yaml` install Go with
  `actions/setup-go` (version from `src/go/helper/go.mod`) and so run
  `regression/go`; other jobs skip it.
- Building the helper needs Go 1.27 (`go.mod`, `GOTOOLCHAIN=local`).
- At verification time, programs that import only `cprover` and packages
  of their own module need no Go toolchain. Standard-library imports go
  through the gc importer, which runs `go list -export`, and packages of
  other modules are located with the go command, so these need `go` on
  `PATH` (as in the CI jobs that run `regression/go`, whose stdlib tests
  need it).

Checks beyond the regression tests, in `scripts/go-frontend/`:

- `check_spec_quotes.py` checks that comments quote the specification
  verbatim and cite existing sections.
- `run_with_go.sh` runs every test with the Go toolchain, using the real
  `cprover` package (and so the real standard library rather than the
  models), as an oracle: each test that CBMC is expected to verify must
  also run to completion. Tests are directories: all their files, and a
  `go.mod` with further packages, are used.
- `check_non_vacuous.sh` appends a failing assertion to `main` of each
  success test and checks that CBMC reports it.

Known limitations:

- **The standard library is models only.** Calls of unmodelled functions,
  methods of standard-library types, and variables of standard-library
  packages (e.g., `io.EOF`, `os.Args`) are reported. Packages of other
  modules are converted from source and so may use constructs that are
  not supported yet. The printing functions of `fmt` do not call
  `Format` or `GoString` methods, nor methods of nested values.
- Type declarations inside generic functions are reported: their
  underlying types may involve the type parameters, which the helper does
  not substitute in named types it has not instantiated.
- Generic functions of the standard library without a model are
  reported like other unmodelled functions. Instances are bounded
  by go/types' check for instantiation cycles, and in addition by 2000
  instances.
- Values containing pointers, functions, maps or interfaces cannot be
  nondeterministic: `cprover.Nondet` of such types and `--function` with
  such parameters are reported. Modelling them needs a choice of nil or
  some variable (or function) of the right type, including aliasing
  between them. `cprover.Nondet` supports strings and slices (of any
  length, in a new underlying array; slices may be nil), also within
  structs; `--function` does not support them yet.
- **Strings and maps need loop bounds.** String comparisons, UTF-8
  conversions and all map operations loop; `--unwind` must cover the
  string lengths and map sizes, which `--unwinding-assertions` checks.
- The string representation was chosen after studying the Python
  front-end's iterations (branch `tautschnig/py`,
  `doc/python-frontend-strings-plan.md` there): Python strings are code
  point sequences, for which it ended up with CBMC's string refinement
  (`--refine-strings`, default) and SMT-LIB `String` (opt-in, needing
  cvc5 or z3, with fixed-width handles because variable-width values
  must not appear in structs). Go strings are byte sequences with byte
  indexing and shared substrings, which a pointer and a length over
  ordinary arrays represent exactly, with the default SAT back end, and
  inside structs, arrays, slices and maps. String refinement or SMT
  strings could later speed up comparisons of long symbolic strings.
- **Evaluation order.** Where `§Order_of_evaluation` leaves the order
  open (e.g., reading a variable relative to a call in the same
  expression), the converter picks one order, so a program whose
  correctness depends on that order is only verified for that one.
- Pointers to distinct zero-size variables compare equal
  nondeterministically (if neither is nil), as the spec allows.
- Function-pointer removal adds a `pointer_dereference` property per call
  through a function value ("dereferenced function pointer must be
  ..."). It always holds, because function values are only ever nil
  (checked first) or the address of a function of that type.
- **Fused floating-point operations.** `§Floating_point_operators`
  allows an implementation to "combine multiple floating-point
  operations into a single fused operation, possibly across statements",
  and to keep float32 values at higher precision. The converter models
  each operation rounded separately, as gc does on amd64; gc fuses
  `x*y + z` on some architectures (e.g. arm64), so results proved here
  need not hold there for programs sensitive to that rounding.
- Deferred calls of built-in functions (other than `recover`, which then
  has no effect) are reported. The list of deferred calls is a loop, so
  `--unwind` must cover the number of deferred calls of a function whose
  defer statements are in loops. When a deferred function panics while
  panicking, only the latest panic is reported. The messages of run-time
  errors are gc's without the operands (e.g., no index in "index out of
  range"), as the specification leaves them unspecified.
- Not yet supported: `select`, `range` over functions and channels,
  goroutines, and channels.

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

1. **Control flow and functions** (done). `§Labeled_statements`,
   labelled `§Break_statements`/`§Continue_statements`,
   `§Goto_statements`, `§Switch_statements` (expression switches,
   `fallthrough`), multiple results and `§Return_statements` with
   several values, `§Calls` with multi-valued arguments,
   `§Function_literals` without captures. Closures that capture
   variables need variables that outlive their function, so they move
   to phase 2, together with pointers.
2. **Composite value types** (done). `§Array_types`, `§Struct_types`
   (including embedded fields and promotion, using the helper's `_sel`
   paths), `§Pointer_types` with nil-dereference panics,
   `§Address_operators`, `§Composite_literals`, `§Comparison_operators`
   for arrays and structs, index-out-of-range panics
   (`§Index_expressions`), closures that capture variables
   (`§Function_literals`), and `§For_range` over integers, arrays and
   pointers to arrays.
3. **Strings, slices and maps** (done). `§String_types` (immutable byte
   sequences, `len`, indexing, `§Slice_expressions`, `+`, comparisons,
   `§For_range` over strings with UTF-8 decoding), `§Slice_types` with
   `make`, `append` and `copy` (`§Appending_and_copying_slices`),
   `§Map_types` with nondeterministic iteration order, `delete`, `clear`.
4. **Floating point** (done). `float32`/`float64` per IEEE 754
   (`§Arithmetic_operators`), numeric conversions (`§Conversions`), and
   `complex64`/`complex128`.
5. **Methods and interfaces** (done). `§Method_declarations`, method values and
   expressions, `§Interface_types` with dynamic dispatch,
   `§Type_assertions` and `§Type_switches` (using `_impl`), the `error`
   interface.
6. **Generics** (done). `§Type_parameter_declarations` and
   `§Instantiations` (also generic methods [Go 1.27]), by monomorphising
   each instantiation `go/types` reports.
7. **Panics and deferral** (done). `§Defer_statements`,
   `§Handling_panics` (`recover`), so that a run-time panic becomes an
   unwinding path instead of an assumption.
8. **Packages and the standard library** (done). Several files per
   package, imported user packages, variadic functions, and Go models of
   the stdlib functions programs use (§4.5).
9. **Concurrency.** `§Go_statements`, `§Channel_types`,
   `§Send_statements`, `§Receive_operator`, `§Select_statements` with a
   nondeterministic choice, and the Go Memory Model for data races.

Phases 1–3 cover most small verification harnesses. All panics go
through one helper (`go_convertert::raise_panic`, used by `add_panic`
and `guard_panic`), which made phase 7 a local change.
