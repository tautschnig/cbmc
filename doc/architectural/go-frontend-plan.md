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

This governs goroutines, channels, `sync`, and data races (phase 9).
Cite as `GoMem §<anchor>`.

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

The first end-to-end slice and phases 1 to 9 of §6 are done. The code is in
`src/go/`, the tests are in `regression/go/`.

| Part | Files | Role |
|---|---|---|
| Helper | `helper/main.go`, `helper/packages.go`, `helper/cprover/`, `helper/runtime/`, `helper/models/` | `go/parser` + `go/types` → typed JSON (schema version 1). Type-switch bindings (`_impl`, from `types.Info.Implicits`), receiver-qualified method ids, file-qualified positions and the bytes of string constants (`_sb`) are emitted. Each type entry has a canonical id `tid` (the first entry of a `types.Identical` class), and each non-interface type its method set `mset` (name, method object, embedded-field path, whether the receiver is reached through a pointer, whether it is a pointer receiver, and the method-value type). Generic code is monomorphised here (see below), so the converter sees no type parameters. The runtime package `cprover/rt` (`helper/runtime/rt.go`) is type-checked and emitted with every program. The files given to it form the program's package; the user packages it imports are loaded from source, and models of standard-library packages (`helper/models/`) where the program imports those (see below). Each package has its initialisation order (`packages`, in the order in which the packages are initialised). |
| Go AST | `go_parse_tree.{h,cpp}` | In-memory `irept` tree loaded from the JSON. The converter reads only this tree (§4.3). |
| Types | `go_types.{h,cpp}` | `go/types` type table → `typet`. `int`/`uint`/`uintptr` come from `config.go`. Named struct types become struct tags (type symbols named like objects, e.g. `go::main.Node`, and `go::main.Pair[int,string]` for instances of generic types), so recursion through pointers is finite. Named function types that refer to themselves (e.g. `type stateFn func(*lexer) stateFn`) are struct tags of their function values, for the same reason; other named types, including other named function types, are their underlying type, and other types that refer to themselves (e.g. `type T []T`) are reported. Blank fields are kept under the names `_$N`. Booleans are stored in a byte (`c_bool`), so that they have a size (for heap variables and arrays); `as_condition` turns them into conditions. `float32`/`float64` are IEEE 754 `floatbv` types; complex numbers are structs `{real, imag}` marked `#go_complex` (CBMC's complex type is not lowered in function types). Strings, slices and maps: see below. Function values are structs `{code, env}` marked `#go_func_value` (see below); several results are returned as one struct marked `#go_tuple`, with components `$0`, `$1`, ... All interface types are one struct `{type int32, data *void}` marked `#go_interface` (see below). Each type records its canonical Go type id in the comment `#go_tid`. |
| Converter | `go_convert.{h,cpp}`, `go_builtins.cpp`, `go_maps.cpp`, `go_floats.cpp`, `go_interfaces.cpp`, `go_panics.cpp`, `go_channels.cpp` | Go AST → symbol table and code. Constructs outside the slice are reported as errors, never dropped. Strings, slices and their built-ins are in `go_builtins.cpp`, maps in `go_maps.cpp`, floating-point and complex numbers in `go_floats.cpp`, methods and interfaces in `go_interfaces.cpp`, panics, `defer`, `recover` and `go` statements in `go_panics.cpp`, channels and `select` in `go_channels.cpp`. |
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
- Package unsafe (`§Package_unsafe`): `unsafe.Pointer` is a void
  pointer. Conversions between it and pointers are casts, and between it
  and `uintptr` CBMC's pointer-integer casts; `unsafe.Pointer(uintptr(p)
  + off)` (with further `+`/`-` of offsets) is pointer arithmetic on `p`,
  so the result points into `p`'s object, as with `unsafe.Add`.
  `unsafe.Slice` and `unsafe.String` build the descriptors with gc's
  panics (negative length, nil pointer with a length, a size that
  overflows `uintptr`; gc's check that the memory does not wrap around
  the address space is not made); `SliceData` and `StringData` are the
  data pointers. Dereferencing a pointer converted from another type
  reinterprets the bytes (CBMC's byte-level encoding, in the target's
  byte order). `cprover.Nondet` of `unsafe.Pointer` is reported.
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
  grow, the spec does not fix the new capacity. As the C front-end
  follows the configured C compiler, the Go front-end follows the Go
  compiler: the helper reports the compiler of the `go` command on PATH
  (`go version`), and `--go-compiler gc|any` overrides it. For gc go1.27
  the capacity is runtime.growslice's (`rt.GrowCapGC`: `nextslicecap`,
  rounded up to a malloc size class, with the malloc header of
  pointerful objects above 512 bytes) or, as gc may back slices that do
  not escape by a 32-byte stack buffer, the whole buffer on the first
  append to an empty slice that fits (`rt.StackCapGC`) or the length
  rounded up to a size class (`rt.RoundUpCapGC`, as `growsliceBuf` and
  `moveSliceNoCap`), chosen nondeterministically, as escape analysis
  decides between them. The array has room for the largest of these, so
  that its size is constant when the lengths are. gc's element sizes and
  whether elements have pointers come from the helper (`esize`, `eptrs`
  of slice types). TinyGo 0.44 (`--go-compiler tinygo`, which cannot be
  detected, as TinyGo uses the `go` command's standard library) gives
  the smallest power of two above the new length (`rt.GrowCapTinyGo`),
  also to slices that do not escape. For other compilers and versions
  (gccgo and gollvm, which use libgo's Go 1.18 runtime, GopherJS, whose
  rule `rt.go` documents, other gc releases), any capacity that fits is
  possible. `src/go/helper/runtime/rt_test.go` checks the model against
  the gc that runs it (with and without `-gcflags=all=-N`), and
  `rt_tinygo_test.go` against TinyGo (`-opt=z` and `-opt=0`, if
  installed), in ctest and `make -C regression/go test`; CI installs
  TinyGo in one job. `scripts/go-frontend/run_with_go.sh` runs tests with
  `--go-compiler tinygo` with `tinygo run`.
  Element ranges are copied as by CBMC's `memcpy` model: with
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
  `Unwrap`, `AsType`, `Join`, and `As`, with the intrinsics
  `rt.AsTarget` and `rt.AsAssign` instead of reflectlite), `strings`
  (now the real package with these functions replaced, see below:
  `Compare`, `Contains`, `ContainsRune`, `Count`, `Cut`, `CutPrefix`,
  `CutSuffix`, `Fields`, `HasPrefix`, `HasSuffix`, `Index`, `IndexByte`,
  `Join`, `LastIndex`, `Repeat`, `Split`, `TrimPrefix`, `TrimSpace`,
  `TrimSuffix`), `strconv` (`FormatBool`, `FormatInt`, `FormatUint`,
  `Itoa`), `unicode/utf8` (likewise: `AppendRune`, `DecodeRune`,
  `DecodeRuneInString`, `EncodeRune`, `RuneCount`, `RuneCountInString`,
  `RuneLen`, `Valid`, `ValidRune`, `ValidString`, which use the
  runtime's decoding), `unicode` (the real functions for Latin-1, with
  the real `properties` and `asciiFold` tables, and `IsSpace` and
  `IsControl` for all runes; beyond Latin-1, results that need the
  Unicode tables are arbitrary, a valid rune for the case mappings, as
  the tables are too large to convert), `internal/abi` (`NoEscape`),
  `math` (`Abs`,
  `Ceil`, `Copysign`, `Float32bits`, `Float32frombits`, `Float64bits`,
  `Float64frombits`, `Floor`, `Inf`, `IsInf`, `IsNaN`, `Max`, `Min`,
  `Modf`, `NaN`, `Signbit`, `Trunc`), `slices` (`Contains`, `Equal`,
  `Index`, `Max`, `Min`, `Reverse`; now the real package, see below) and
  `fmt` (the printing functions `Print`, `Sprint`, `Fprint`, `Append` and
  their `ln` and `f` variants call the `Error` or `String` method of each
  argument, recovering from panics in them; the text is arbitrary, except
  for `Sprint` of one string; `Fprint*` write it with one `Write` call,
  and the byte counts and errors of `Print*` are arbitrary. `Errorf`
  returns errors of fmt's types and wrapping: for a constant format, the
  helper rewrites the call to the model's `errorfKnown` with what the
  format determines (the number of `%w` verbs and their operands, by
  fmt's operand numbering, which `models_test.go` checks against fmt; and
  whether the text is the format); otherwise any shape is possible).
  Models of
  package-level variables work the same way: a variable of the model
  with the name and an identical type stands for the real one.
  *Source models* (`sourceModels` in `models.go`) are the packages' own
  source from GOROOT, type-checked as `cprover/std/<path>`: `cmp`,
  `math/bits` (with its bootstrap errors), `internal/byteorder`,
  `internal/bytealg` (the portable files, `MaxLen` 0), `slices`
  (`overlaps` replaced, as it compares addresses through `unsafe`),
  `sort` (with `Slice`, `SliceStable` and `SliceIsSorted` replaced, as
  they use `internal/reflectlite`), `strconv` (with `internal/strconv`
  and `internal/stringslite`,
  whose conversions through `unsafe` are replaced; the integer
  formatting is the earlier model's), `strings` (`strings.Builder`,
  `Replacer`, ...), `bytes` (`bytes.Buffer`, ...), `unicode/utf8`, `io`,
  `encoding/binary` (whose `Read`, `Write` and `Size` use reflection, and
  are reported when called), `encoding/json` (go1's implementation,
  which go1.27 builds without `GOEXPERIMENT=jsonv2`: the helper uses the
  standard library's export data built without that experiment, so that
  the real package's types are those of the source; its behaviour
  differs from go1.27's default, json/v2 underneath, in details such as
  error messages), `encoding/base64` (with `NewEncoding`'s loop over the
  64 symbols unrolled, as the package's initialisation calls it),
  `math` (the files of an architecture without
  assembly, so the functions are portable Go, with the bit conversions
  and rounding of the earlier model; `Sqrt` is a loop of up to 54
  iterations), `math/big` (its pure-Go arithmetic), `time` (see below),
  `encoding/hex`, `hash/fnv` and `unicode/utf16`. Variables of source models (e.g. `io.EOF`,
  `binary.BigEndian`) are the real ones. Files of `models/<path>` then replace the functions
  of the same names. The named types of a source model are encoded as
  the real package's types (with the model's methods), so that the
  program's type assertions and switches see the values the model
  creates (e.g. `*strconv.NumError`) as with gc. Functions without a
  body (assembly, linkname) fail when called. `unsafe.Sizeof` and
  `unsafe.Alignof` of types that depend on type parameters are
  constants in each instance (gc's sizes from the helper). Constants of standard-library packages
  (e.g. `math.MaxInt64`) are folded by go/types as before. The runtime
  package provides intrinsics that the converter implements:
  `rt.Comparable` (whether a dynamic type is comparable, for
  `errors.Is`), `rt.SameType` (whether two interface values have the
  same dynamic type, for `atomic.Value`), `rt.SliceLen` and
  `rt.SliceSwap` (the length of a slice of any type in an interface
  value and swapping its elements, for `sort.Slice`) and the IEEE 754 bit
  conversions (byte extraction).
  In the models (`cprover/...`, and the replaced packages below), the
  capacities of `append` are gc's for slices on the heap: the stack
  buffers of slices that do not escape are not considered (the models'
  internal capacities are not observable), so that the arrays of their
  buffers stay unique (marshalling a struct with `encoding/json` and
  unmarshalling the result: out of time after 20 minutes, 20 s now).
  `sync` and `sync/atomic` are *replaced* by their models: their types
  keep state in internal fields that function models cannot reach, so
  programs are type-checked against the models (and only their API is
  available). Modelled: `sync` (`Mutex`, `RWMutex`, `Locker`,
  `WaitGroup` with `Go`, `Once`, `OnceFunc`, `OnceValue`, `OnceValues`,
  `Cond`, `Map`, `Pool`) and `sync/atomic` (the integer functions and
  the types `Bool`, `Int32`, `Int64`, `Uint32`, `Uint64`, `Uintptr`,
  `Pointer[T]`, `Value`, and the functions on `unsafe.Pointer`). `Pool` keeps
  or drops each item put, and `Get` returns any kept item or none,
  nondeterministically (the real pool may drop items at any time).
  `Map.Range` visits the keys present when it starts, with their values
  when visited, and then possibly keys stored meanwhile. Copying a
  `Cond` is not detected, and the comparison and swap of
  `Value.CompareAndSwap` are one atomic step. In `time` (a source model),
  the runtime's functions are models (`models/time`): the wall clock is
  arbitrary at each reading (seconds since 1970 below 2^32), the
  monotonic clock never decreases, and `Sleep(d)` advances it by at least
  d; a timer fires in a goroutine of its own, so at any later point of
  the execution unless it is stopped or reset first, and firing advances
  the monotonic clock to the timer's time; channel timers buffer one
  value (as before Go 1.23, so a value sent before `Stop` or `Reset` may
  still be received); the local time zone is a fixed zone with an
  arbitrary offset. They use the
  runtime intrinsics `rt.AtomicBegin`/`rt.AtomicEnd` (CBMC atomic
  sections) and `rt.Fatal` (property class `fatal`, for gc's fatal
  errors such as "sync: unlock of unlocked mutex", which `recover`
  cannot stop); blocking operations assume that they can proceed.
- `reflect` is replaced by its model (`models/reflect`, with
  `go_reflect.cpp`). Unlike `sync`, programs are type-checked against
  the real package (from export data), as other standard-library
  packages' signatures mention its types (`encoding/gob`,
  `text/template`, ...); the encoding maps the real package's functions,
  methods and types to the model's of the same names, and type identity
  in the type table is modulo this mapping. A type is a descriptor (`reflect.rtype`), a global
  that the front-end builds for each type a reflected value may have:
  the dynamic types of interface values and the types they refer to
  (elements, keys, fields, parameters, results, method types, underlying
  types, and the pointer type of each, which the helper adds to the type
  table for `New` and `Addr`; with `SliceOf`, also the slice type of
  each). The described types are dynamic types (`Value.Interface` may
  box them). Descriptors hold what reflect reports, from the helper
  (gc's spelling with package names, `Name`, `PkgPath`, gc's sizes,
  alignments and field offsets, tags, method sets sorted by name), and
  the front-end's own layout (the size of a value and the offsets of
  fields in CBMC's structs) for the model's address arithmetic. Type
  identity is identity of descriptors. A `Value` is the descriptor of
  its type, a pointer to the value (to an immutable copy if it is not
  addressable) and flags (addressable, obtained through an unexported
  field, method value). The model reads and writes values of basic types
  through `unsafe` pointers; the rest are intrinsics of the model whose
  bodies test the type id against each described type: making interface
  values, copying, allocating, `==`, the map operations, the headers of
  slices, maps and channels, finding a constructed type (`SliceOf`,
  `MapOf`, `ArrayOf`, `ChanOf`), and calling functions and methods
  (`Value.Call`, method values; generated only if the program calls
  through reflect, as the helper records in `reflect_uses`). Modelled:
  `Type` (kinds, strings, elements, fields with tags and embedded-field
  search, methods, `Implements`, `AssignableTo`, `ConvertibleTo`,
  overflow checks, iterators), `Value` (reading and setting, `Elem`,
  `Field*`, `Index`, `Slice`, `Len`, `Cap`, `IsNil`, `IsZero`,
  `Convert`, `Addr`, `Pointer`, `UnsafeAddr`, maps, `Append`, `Copy`,
  `Grow`, `Call`, `Method`), `New`, `NewAt`, `Zero`, `MakeSlice`,
  `MakeMap`, `Indirect`, `DeepEqual`, `Swapper`, `TypeFor`,
  `TypeAssert`, `VisibleFields`, `StructTag`; operations that it does
  not support fail a property of class `unsupported`
  (`rt.Unsupported`), see §7.1. `SliceHeader` and `StringHeader` work
  through the support of `unsafe`.
- Deadlocks (property class `deadlock`, gc's "all goroutines are asleep
  - deadlock!"; `--go-no-deadlock-check` disables them). At each
  blocking operation (channel operations, `select` without `default`,
  and the models' `rt.AtomicBeginWhen` for `Mutex`, `RWMutex` and
  `WaitGroup`), a goroutine that cannot proceed may instead park
  (nondeterministically) and never continue: it counts itself in
  `go::$parked`; once all live goroutines (`go::$live`, counting main,
  which stays live after returning, so that the program then ends rather
  than deadlocks) have parked, no goroutine changes the state any more,
  and each confirms that it still cannot proceed; when all have
  confirmed, an assertion at the blocking operation fails. A parked
  `select` waits as a receiver on its unbuffered channels, so that a
  sender sees it, and the operations of a committed `select` case do not
  park. The parking has atomic sections of its own, before the
  operation's: goto-symex's atomic sections must not span branches that
  leave and re-enter them. The check costs about 2.5 to 4 times the
  verification time of the channel and sync tests.
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
  is not comparable (`uncomparable`), channel operations on closed or
  nil channels (`closed-channel`), negative channel sizes (`makechan`),
  and `panic` (`panic`).
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
- Concurrency (`§Go_statements`, `§Channel_types`, `§Send_statements`,
  `§Receive_operator`, `§Close`, `§Select_statements`, GoMem §chan). A
  `go` statement evaluates the function value and arguments (as `defer`
  does) and starts a CBMC thread (`__CPROVER_ASYNC_1`) that calls the
  generated function `<function>$goN` and then `go::$check_panics`, so
  that an unrecovered panic of a goroutine is reported. A channel is a
  pointer to a header on the heap (capacity, count, head index, closed
  flag, send and receive tickets, waiting receivers, buffer), updated in
  atomic sections; an operation that cannot proceed blocks by assuming
  that it can (`go_channels.cpp`). On an unbuffered channel the sender
  waits until a receiver has taken its value. `select` without a
  default case takes a case that can proceed, or commits to one case and
  performs it as a blocking operation, so that two goroutines that both
  select can communicate.
- Pointers shared between threads. CBMC's symex used to reject programs
  that assign shared pointers ("pointer handling for concurrency is
  unsound", GitHub issue #305), which every program that shares a
  channel or a closure between goroutines does. Symex now repeats
  symbolic execution until the value sets of shared objects contain
  every value a thread may write to them
  (`src/goto-symex/shared_value_summary.h`), with deterministic names
  of the objects symex creates; path exploration (`--paths`) still
  rejects such programs. On the way, two existing symex bugs were fixed
  (reads in atomic sections on different branches saw different values;
  starting a thread in a loop with a struct local failed an invariant),
  and atomic sections may now be left early on one branch.
- Range over functions (`§For_range`, `go_range_func.cpp`). The body
  becomes the yield function, a closure over the variables of the
  enclosing function it uses and over three it shares with the loop: the
  state of the iteration, why the body ended it (break, return, or a break
  or continue statement whose target is outside the loop), and the
  results of a return statement. After the iterator returns, the loop
  does what the body asked for (also through nested loops). As in gc,
  calls of yield after the body ended the loop, after the loop, or after a
  panic of the body that the iterator recovered are run-time errors
  (property class `range-func`).
- Verification primitives from package `cprover`: `Assert`, `Assume`,
  `Nondet[T]`.

Errors are reported as follows:

- Syntax and type errors from the helper are printed as
  `file:line:col: message` and end in `PARSING ERROR`.
- A call of a standard-library function or method without a model is
  reported as a warning and becomes a failing property of class
  `unsupported` where it is (its arguments are not evaluated, and its
  result is arbitrary); a dispatcher of interface method calls does the
  same for an unmodelled method of a dynamic type. Other unsupported
  constructs in the body of a function are reported as warnings, and the
  function's body becomes a failing property of class `unsupported` that
  names the first of them. They are never dropped silently: a program that calls
  the function fails to verify, and only programs that do. This lets
  programs use packages (copied or real) of which some functions are not
  supported. Declarations are handled alike: a package variable, function
  or method whose type is unsupported is reported as a warning; a call
  of such a function or method fails a property where it is (as for
  unmodelled functions, if its result type is supported), and other
  uses of it are unsupported constructs of the function that has them.
  An unsupported initialiser of a package variable fails a property
  where package initialisation reaches it (so before `main`).
  Unsupported constructs elsewhere (other declarations, entry-point
  parameters) end in `CONVERSION ERROR`.
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
  `Format` or `GoString` methods, nor methods of nested values, and their
  text is arbitrary: properties that depend on it may fail where gc's
  output would satisfy them (e.g., parsing the result of `Sprintf`).
- Type declarations inside generic functions are reported: their
  underlying types may involve the type parameters, which the helper does
  not substitute in named types it has not instantiated.
- Generic functions of the standard library without a model are
  reported like other unmodelled functions. Instances are bounded
  by go/types' check for instantiation cycles, and in addition by 2000
  instances.
- Arbitrary values (`cprover.Nondet`, and the arguments of the
  `--function` target, which the generated `go::$entry` builds) are
  built as CBMC's object factory builds C inputs, with its options
  `--max-nondet-tree-depth` (default 5) and `--min-null-tree-depth`:
  strings and slices of values without references have any length and
  arbitrary contents in a new underlying array (slices may be nil); a
  pointer is nil or points to a new arbitrary value; a map is nil or has
  at most `max_nondet_array_length` (5) entries with distinct keys
  (allocated with that capacity, so that its arrays have a constant
  size); slices of values with references have at most that capacity;
  an interface value is nil or holds an arbitrary value of one of the
  program's types that implement it (types with its methods, from the
  helper's method sets; for interfaces without methods, the types the
  program converts to interfaces), from a generated function per
  interface type and depth (`go::$nondet.<tid>.<depth>`); structs and
  arrays (of at most 256 elements with references) are built field by
  field. References beyond the maximum depth are nil. These are
  under-approximations: objects are not shared between references (no
  aliasing), and values of types outside the program are not built.
  Function values and channels are not supported (calls of the function
  with `cprover.Nondet` of such a type fail; `--function` reports
  them).
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
- **Concurrency.** Data races are not reported yet: by GoMem §model,
  data-race-free programs behave sequentially consistently, which is what CBMC encodes
  (racy programs are verified under sequential consistency, which gc
  does not guarantee). Reporting races is required future work (see
  §6, phase 10). A goroutine that blocks forever just does not
  continue; when all do, the deadlock is reported (see Deadlocks above),
  within the bounds of the unwinding. `select` without a default case
  over-approximates: it may commit to a case before it can proceed (and
  then wait for it), which gc only does for a case that can proceed, so
  a property may fail in an execution gc never produces. Loops in
  goroutines, and goroutines started in loops, need `--unwind`; a bound
  too small for a loop that starts goroutines leaves their executions
  infeasible but still explored, which can be slow. The pseudo-random
  choice of `select` among ready cases is nondeterministic. Values that
  CBMC's value sets track imprecisely (e.g. byte-level copies of
  pointers) make calls through function values read from shared objects
  consider every function of their type.
- **Range over functions.** Deferred calls in the body of a range over a
  function (which belong to the enclosing function) and goto statements
  that leave the body are reported.

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
9. **Concurrency** (done). `§Go_statements`, `§Channel_types`,
   `§Send_statements`, `§Receive_operator`, `§Select_statements` with a
   nondeterministic choice, models of `sync` and `sync/atomic`, and
   sound pointer handling for threads in goto-symex. Data races are not
   reported (see the limitations).
10. **Data races** (open, required). Report the data races of GoMem
   §model as failing properties. goto-instrument's `--race-check` only
   instruments accesses to global variables, while Go's shared objects
   are mostly on the heap (captured variables, slices, maps, pointees),
   so this likely needs shadow memory that records, per object, the
   last unsynchronised write and the reads since, with the happens-before
   edges of channels, `sync` and `sync/atomic` clearing them.

Phases 1–3 cover most small verification harnesses. All panics go
through one helper (`go_convertert::raise_panic`, used by `add_panic`
and `guard_panic`), which made phase 7 a local change.

## 7. Open problems

Collected while converting a corpus of real code (195 third-party
packages of 40 modules, and copies of 165 standard-library packages,
`~/go-scratch/corpus`, a harness per package that imports it). All of
the third-party packages convert, and all standard-library copies except
those of `encoding/json` (v1 over v2), `encoding/json/v2` and
`encoding/json/jsontext`, which need the jsonv2 experiment that the
helper turns off (§5; so programs that import `encoding/json/v2` are not
supported). Unsupported constructs are reported where they are reached:
15,781 sites (25,108 before reflect was modelled, of which 12,375 were
calls of reflect; 19 calls of `internal/reflectlite` remain), almost all
of them calls without a model (`go/types`, `syscall`, `unicode`'s
tables, `os`, `go/ast`, `bufio`, `regexp`, `text/template/parse`, ...).
By the study of reflect's uses in the builds of the 100 most popular Go
applications (`~/go-scratch/reflect-study`), 97% of the packages that
use reflect use only members that the model supports, and a further 2.7%
use `MapOf`, `ArrayOf` or `ChanOf`, which find the program's types.

### 7.1 Soundness and required features

- **Data races** are not reported (§6, phase 10). Required.
- **`reflect`** is modelled (§5), but not completely: `Select`,
  `MakeFunc`, `FuncOf`, `StructOf`, the channel operations of `Value`,
  `Method.Func` and `Method.Type` of non-interface types, and
  `Value.Interface` of method values are reported; `MapOf`, `ArrayOf`
  and `ChanOf` find only the types the program has; `MapKeys` and
  `MapRange` visit the entries in the map's order (gc's is
  unspecified, so this misses executions); `DeepEqual` does not detect
  cycles. Loops over tags (`StructTag.Lookup`, `strconv.Unquote`, and
  encoding/json's `isValidTag`, with a 28-character `ContainsRune`)
  need loop bounds of the tags' lengths. `fmt` does not use it yet (the
  formatting of nested values), nor `encoding/binary.Read`/`Write`.
- **Arbitrary results of models** are sound but imprecise: the text of
  `fmt`'s printing functions, `unicode` beyond Latin-1 (also
  inconsistent between calls for the same rune), the wall clock and the
  local time zone of `time`, `sync.Pool`, `sync.Map.Range` for keys stored
  meanwhile, `fmt.Errorf` with non-constant formats (any wrapping).
  Properties that depend on them may fail where gc's behaviour would
  satisfy them.
- **Approximations that are not over-approximations**: `atomic.Value`'s
  comparison and swap are one atomic step (gc's `CompareAndSwap` can
  fail on a concurrent store of an equal value); copying a `sync.Cond`
  is not detected; `unsafe.Slice` does not check that the memory wraps
  around the address space; nondeterministic values have no aliasing
  and no function values or channels (§5).
- **Unsupported constructs left in user code** (other than calls): types
  that refer to themselves other than through structs or function types
  (`type T []T`, `type P *P`), type declarations in generic functions,
  closures that capture type parameters (25 corpus sites), deferred
  calls of built-in functions other than `delete` and `close`, `goto`
  out of and `defer` in range-over-function
  bodies, package variables of unmodelled packages (`os.Stdout`,
  `os.Args`, ...).
- **Unmodelled packages** by corpus call sites: `go/types` (1,737),
  `syscall` (1,234, mostly in the file-reading functions of `time`'s and
  `os`'s sources, reached only through them), `go/ast` (666), `os`
  (598), `bufio` (410), `runtime` (290), `go/token`, `log`, `image`,
  `regexp`, `net`, `net/http`, `path/filepath`, `encoding/asn1`. Several
  are plain Go and could be source models as `strings` and `io` are
  (`bufio`, `path/filepath` with `os` stubs, `regexp`, `text/template`
  now that `reflect` is modelled); `os` and `syscall` need models of the
  environment (files, arguments, environment variables).
- **Models track one Go release.** Source models are the GOROOT source
  of the toolchain, but the function models, the overrides and the
  copied tables (`unicode`'s `properties` and `asciiFold`, the
  White_Space runes) are go1.27's, and `errorfWraps` follows go1.27's
  `doPrintf` (checked by `models_test.go`).

### 7.2 Performance and scalability

- **`errors.Is` on errors of several possible dynamic types.** For two
  types this is fixed (out of memory at `--unwind 3` before, 0.14 s
  now) by three changes: `unbox` reads the boxed value through a typed
  dereference (`ID_C_typed_dereference`), so that the value sets of the
  data pointer do not mix the objects of all dynamic types; the returns
  of unwinding functions are zero values rather than nondeterministic
  ones; and goto-symex propagates values merged from at most 8
  constants (`(g ? c1 : c2)`), so that type-id comparisons simplify.
  Three types (`errors-is-several-types`) still grow exponentially with
  `--unwind` (36 s at 3): each level of the `Unwrap` recursion explores
  the branch of every type. Dispatch through the value set of the data
  pointer (as function-pointer removal does) remains a possible remedy;
  pointer-typed type ids and type ids stored in the boxed object did
  not help in experiments.
- **`append` with a nondeterministic capacity** (gc's capacities are not
  determined, §5) gives a slice several possible underlying arrays,
  which multiplies the objects that later accesses may refer to: 16
  appends to a `[]int` take 98 s with gc's capacities and 0.4 s with
  `--go-compiler tinygo` (114 s for `[]any`). The models use heap
  capacities (§5), so this only concerns the program's own slices.
  gc's escape analysis
  (`-gcflags=-m`, or `-json=0,file://dir`) tells which slices stay on
  the stack, whose capacities are then determined, but inlined copies
  of a function make the mapping to source positions ambiguous.
- **Objects**: every conversion to an interface allocates the value.
  Go programs now default to 16 object bits (at most half the pointer
  width), as Java programs do; `sync-map` no longer needs
  `--object-bits`. Boxing without allocation where values are not
  shared would reduce the objects further.
- **Concurrency** is costly in the solver: two goroutines that update a
  `map[any]any` under a mutex take about 300 s and 12 GB (22 million
  variables, 94 million clauses), dominated by the constraints of
  `memory_model`. The shared reads are mostly of the map's key and
  value arrays and of the work of `$interface_equal`, which dispatches
  over all dynamic types. The panic state is already thread-local. The
  timer test (`stdlib-time-timers`, THOROUGH) takes 200 s, and the
  deadlock check multiplies solver time by 3 to 5 in the concurrency
  tests (§5).
- **The fixed cost of source models**: every program converts all of
  the source models it imports transitively (`strings` alone: 2.4 MB of
  JSON, 1.5 s; `math/big`: 11 MB, 11 s, after sharing the entries of
  identical composite types in the helper, which made it 152 MB and
  29 s before). The helper sends array and slice literals of at least 16
  integer or boolean constants as lists of values (`_cvals`) and omits
  the operands of constant expressions, so generated tables are cheap:
  the `x/text` harnesses for `unicode/runenames` (628 MB of JSON before,
  11 MB now) and `date` (1.4 GB, now 43 MB) convert and verify in 6 s
  and 16 s, and the `stdlib-source-math-big` test takes 10 s instead
  of 54 s. Signatures (25,000 entries for `x/text`'s pipeline, 87.5 MB
  of JSON) are still not shared, and unreachable functions are not
  pruned in the helper.
- **Loops in models need bounds**: string comparisons (`--unwindset
  go::cprover/rt.StringEqual.0:N` for long constants), map operations,
  `math.Sqrt` (54 iterations), the word loops of `math/big`,
  `time.ParseDuration`, `strings`' searches. Users have to tune
  `--unwind`; models could avoid loops where CBMC has the operation
  (e.g., floating-point square root).

### 7.3 CBMC changes to upstream

- goto-symex: propagation of values merged from constants
  (`regression/cbmc/symex-merged-constants`); typed dereferences for
  front-ends (`ID_C_typed_dereference`); sound value sets for pointers
  shared between threads,
  with repeated symbolic execution (`shared_value_summaryt`); objects of
  a previous run that keep their names when their types differ (no C
  regression test reproduces it; the Go test `sync-map-concurrent`
  does); a new thread starts outside any atomic section.
- Linking: skip the renaming passes over the whole program when nothing
  is renamed (455 s to 27 s for a large Go program; benefits C too).
- Array theory (Ackermann constraints) and the profiler's timeout.
- The branch needs splitting for review (front-end, helper, models,
  symex, linking).

### 7.4 Infrastructure

- The Makefile build of `cbmc` does not link the Python front-end (§5).
- `go vet` of the helper reports the models, which import `cprover` and
  `cprover/rt` (not in GOROOT); a `go.work` or a vendored `cprover`
  module would make the models checkable with the Go tools.
- Tests that use unmodelled functions as examples (`os.Getenv`,
  `regexp.Regexp`, `os.Stdout`) must change as models are added.
- The corpus counts include sites inside source models that programs
  rarely reach (e.g., `unicode.SpecialCase` in `strings`, `syscall` in
  `time`); a count of sites reachable from the harness would track
  progress better.
