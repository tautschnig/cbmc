# Native C++ Go parser (spike B)

A standalone lexer and recursive-descent parser for the full go1.27
grammar (`Go1.27 §Lexical_elements` and the productions throughout the
spec). It was written to measure whether a native front-end is feasible.
It is **retained, not discarded**. The front-end starts with the external
`go/types` helper (see `doc/architectural/go-frontend-plan.md` §4), and
this parser is the starting point if or when we pursue a native route.

Run `../check_native.sh` after any change. It builds the parser, checks
it structurally against `go/parser` on the corpus and the whole go1.27.3
stdlib, and checks that invalid inputs are rejected.

## Status (go1.27.3, linux/amd64)

| Check | Result |
|---|---|
| Valid files parsed (stdlib excluding `testdata/`, plus `../corpus/*.go`) | 6,829 / 6,829 |
| Structural agreement with `go/parser` (per-file counts of 21 node kinds) | 0 mismatching files |
| Invalid inputs rejected (`../corpus/invalid/`, each also rejected by `go/parser`) | 12 / 12 |
| Throughput | ~90 MB in ~6.5 s (single thread, `-O2`) |
| Size | 1,242 non-comment lines of C++ (lexer 294, parser 948) |

The 21 kinds compared are FuncDecl, FuncLit, CompositeLit, Call, TypeSpec,
If, Switch (and type switch), For (and range), KeyValue, Return, Labeled,
Send, IncDec, Slice, TypeAssert, Select, TypeParams, ArrayType, and
SliceType. The oracle is `../oracle/`; the comparison is
`../oracle/compare_counts.py`.

## How it got there — bugs found, and how

These show which grammar corners need care, and that acceptance alone is
a weak oracle:

1. **77% of files on the first run.** Function literals as call
   arguments (`t.Run(n, func(t *T) {…})`): `func` in an argument was
   parsed as a function *type* and the body was left unconsumed.
2. **Parameter name vs generic type** (`a [4]byte` vs `G[int]` in a
   parameter list). Resolved by backtracking (see below). After fixes 1
   and 2: 6,826 / 6,827.
3. **Label on an empty statement** (`badloop:` followed by `;`).
   Result: 100% accepted.
4. **Silent mis-parse found only by a stronger oracle.** `src []T` in a
   parameter list was accepted as an instantiation `src[]` with *zero*
   type arguments, followed by type `T`. Everything parsed, and the 18
   kinds compared at the time all matched. Adding ArrayType, SliceType,
   and TypeParams counts exposed 2,102 mismatching files. Fix: `T[` starts
   type arguments only if a type follows (`Go1.27 §Instantiations` does
   not allow empty argument lists).
5. **Generic vs array type declarations.** The first heuristic for
   `type T[P …]` was wrong for `type B [len(x)]int` and for the spec's
   own examples. The spec-faithful rewrite then failed on
   `crypto/x509`'s `[T *net.IPNet | string, V net.IP | string]`, which
   prompted the bracket-depth scan described below.

Lesson for any native work: compare *structure* against the reference
parser, not just acceptance, and track the node kinds that encode each
ambiguity's outcome.

## Ambiguities and how they are resolved

| Construct | Ambiguity | Resolution | Spec |
|---|---|---|---|
| `if x == T{} {` | Composite literal vs block | `expr_lev < 0` in `if`/`for`/`switch` headers. A type *name* followed by `{` is not a composite literal there; parenthesising restores it. Literal types (`[]T{}`) are always allowed. | `§Composite_literals` |
| `type T[P C] …` vs `type T [N]E` | Type parameters vs array length | `starts_type_params()`. Another identifier, `~`, `,`, or a type keyword after `[ IDENT` means generic. With `*` or `(`, it is generic only if a comma appears at the outermost bracket level. `[P *C]`, `[P (C)]`, `[P *C\|Q]` are arrays, as the spec mandates. | `§Type_parameter_declarations` |
| `a [4]byte` vs `G[int]` in parameters | Name + array type vs instantiated type | Try the instantiated type; backtrack to name + type unless the item ends right after it (`,` or `)`). | `§Function_types` |
| `a [N]int` vs `E[int]` in structs | Field vs embedded generic type | Same backtracking: embedded only if the field ends after the type (`;`, `}`, or a tag). | `§Struct_types` |
| `src []E` | Empty instantiation vs name + slice | `T[` starts type arguments only if `]` does not follow immediately. | `§Instantiations` |
| `(a, b int)` vs `(int, string)` | Named vs anonymous parameters | An item followed by something that starts a type was a name. | `§Function_types` |
| `x.(type)` | Type switch guard | Accepted in any type assertion. Validity is a type-checking concern. | `§Type_switches` |
| `<-chan T` vs `<-x` | Channel type vs receive | `<-` followed by `chan` is a type. | `§Channel_types` |
| Semicolons | Implicit statement terminators | Inserted by the lexer after an identifier, literal, `break`, `continue`, `fallthrough`, `return`, `++`, `--`, `)`, `]`, `}` at end of line, and for general comments spanning lines. The parser makes `;` optional before `)` and `}`. | `§Semicolons` |

## Known gaps

None of these is exercised by the go1.27.3 stdlib; each would need a test
before production use.

- `type T[P [2]int] …` (array-typed constraint) is parsed as an array
  declaration.
- **No tree fidelity beyond the counted kinds.** Qualified type names are
  one `TypeName` node rather than a selector; unions in constraints are
  `Union`/`Term` nodes rather than `BinaryExpr` (`go/ast` uses
  `BinaryExpr`); and nodes carry no source positions.
- **Lexer leniency.** Any non-ASCII byte counts as a letter (no Unicode
  category tables). Octal and binary literals accept the digits 8 and 9.
  Underscore placement in numeric literals is not validated. Rune
  literals are not checked to contain exactly one character.
- **No error recovery.** The parser stops at the first error.
- **Comments are dropped**, including directives that matter to a
  front-end: `//go:build`, `//go:linkname`, `//go:embed`, `//go:noinline`.
- `nodet` is a generic tree, not `irept`/`exprt` with `source_locationt`.

## If the native route is resumed

The parser is roughly a tenth of the work. The size of the remaining
work, measured on go1.27.3, is recorded in the plan's §4.2. In order:

1. **Production parser.** Emit the front-end's in-memory Go AST (the same
   one the external helper's JSON is loaded into; see the plan's §5), with
   `source_locationt`. Capture `//go:` directives. Use proper Unicode
   identifier classes and validate literals.
2. **Package loading.** File selection via build constraints
   (`//go:build` expressions and `_GOOS_GOARCH.go` filename suffixes), as
   in `go/build`. Only 587 of the 1,423 files in `fmt`'s dependency
   directories apply on linux/amd64.
3. **Type checker.** Port `go/types` (15.8k LOC): scopes and resolution,
   untyped constants, method sets and embedding, interfaces and type
   sets, generics (inference and unification), assignability and
   conversions. For exact untyped constants, CBMC's `mp_integer` and
   `rationalt` cover integers and rationals. Untyped float constants need
   extended precision (`go/constant` uses `math/big`).
4. **Standard-library signatures.** Either type-check the stdlib from
   source, which requires steps 2 and 3 to handle `unsafe`,
   `//go:linkname`, assembly-backed declarations, and heavy generics, or
   adopt a **hybrid**: the native checker for user code, with stdlib
   signatures pre-extracted by `go/types` into a versioned summary file.

The hybrid removes the Go toolchain from the verification-time path while
keeping the large stdlib problem on the reference implementation. It is
the most promising native-leaning option.
