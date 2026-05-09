\file
# Python strings: proper SMT-LIB String integration (plan a′)

Status: **draft — not yet implemented**.

This plan sketches the architecture of the "option (a′)" refactor
that we parked during the Wave 2 work on the Python `re` module.
Its purpose is to be a concrete design document we can iterate on
before any code lands.

## Background

CBMC has two established string representations:

1. **Refined strings** (`src/solvers/strings/`) — the historical
   representation used by JBMC and by the `--refine-strings`
   back-end. Every string is encoded as a struct
   `{length: int64, content: array-of-char}`; all string operations
   go through `cprover_string_*_func` intrinsics whose meaning is
   given by refinement-loop axioms (e.g.
   `string_constraint_generator::add_axioms_for_equals`).
2. **SMT-LIB strings** (SMT-LIB 2.6 string theory). CVC5 supports
   these as a first-class sort `String`, with operators `str.++`,
   `str.len`, `str.in_re`, `str.contains`, etc. Z3 supports a
   similar subset.

CBMC today chooses (1) regardless of the back-end. The Python
front-end emits refined-string structs and concrete struct
operations (`member_exprt(s, "data", …)`, `struct_exprt{length,
data_ptr}`) directly. Its SMT2 backend (`smt2_conv.cpp`)
converts these structs into bitvector records — the same
encoding whether we're using MiniSat or CVC5.

That architecture was fine when SMT-LIB strings weren't widely
supported by SMT solvers. Today they are, and the refined-string
route is a middle layer that's costing us precision (regex
reasoning is essentially free in CVC5 but we can't reach it).

## Goal

Move the Python front-end to emit string operations *only* through
intrinsics (`cprover_string_*_func`), and let each back-end choose
its own string representation. In particular:

* `--cvc5`/`--z3`-like back-ends pick SMT-LIB `String` and get
  native `str.=` / `str.++` / `str.in_re` / … encodings.
* `--refine-strings` continues to use the refinement-loop axioms.
* JBMC and other refined-string consumers are unaffected.

The frontend becomes representation-agnostic. The back-end
contract is the set of `cprover_string_*_func` intrinsics plus a
construction primitive.

## Phases

### Phase 1: Inventory

Enumerate every site in `src/python/python_converter.cpp` that
reaches into a refined-string's internals. A rough grep
`"data"|"length"|python_string_type\(\).*{|struct_exprt.*python_string`
suggests on the order of 30–50 call sites in the frontend that
need review. For each:

* Classify as producer (constructs a string), consumer (inspects
  a string), or mutator (expresses a string operation).
* Decide the target intrinsic. Most exist already
  (`cprover_string_equal_func`, `cprover_string_concat_func`,
  `cprover_string_length_func`, `cprover_string_contains_func`,
  `cprover_string_char_at_func`, …); a few may need new ones.

### Phase 2: Back-end abstraction

Define a minimal interface the back-ends implement. Each back-end
exposes:

* A type handle for "a string value" — refined-string backends
  return `struct_typet` (the existing shape); SMT-string backends
  return an opaque `smt_string_typet` that `convert_type` maps to
  SMT `String`.
* A set of intrinsic lowerings — for each `cprover_string_*_func`
  intrinsic, how to encode it on this back-end.
* A literal constructor — build a string value from a C++
  `std::string`.

The front-end emits front-end-level IR without caring which
representation is in use; the back-end implements.

### Phase 3: Refactor the front-end

Walk through the inventory from Phase 1 and replace every direct
struct manipulation with the corresponding intrinsic call.
Expected order:

1. String construction — `build_string_struct`, f-string
   formatting, conversion from int/float/bytes, etc.
2. String comparison — already routed through
   `cprover_string_equal_func` for `==` / `!=`; widen to cover
   `<`, `>`, `startswith`, `endswith`.
3. String inspection — `len`, `[i]`, slicing, `in`, `.strip`,
   `.split`, `.replace`, …
4. Any place that reaches into `.data` or `.length` via
   `member_exprt` becomes an intrinsic call.

Each step is test-covered — at least one regression test per
replaced site exercising both backends.

### Phase 4: Back-end implementations

* `smt2_conv.cpp`: implement the intrinsic lowerings for the SMT
  backend. `cprover_string_equal_func` → `(str.= s1 s2)`;
  `cprover_string_concat_func` → `(str.++ s1 s2)`; `…_in_re_func`
  → `(str.in_re s r)`; etc. Add `String` type mapping in
  `convert_type`.
* `string_constraint_generator_main.cpp`: no changes — the
  existing axioms stay as the `--refine-strings` implementation.
* Wire a back-end selector so `smt2_conv` knows whether to use
  SMT strings (when the solver supports them) or fall back to the
  bitvector representation.

### Phase 5: Retire the hacks

Once Phase 4 is in place:

* Remove the subject-literal-only materialisation from
  `smt2_conv`'s `cprover_string_match_func` interception. The
  subject now arrives as an SMT `String` naturally; the
  interception reduces to `(str.in_re subject <regex>)`.
* Replace `cprover_string_equal_func`'s current bitvector-struct
  equality trick with plain `(str.= s1 s2)`.
* Delete the Python→C marshalling helper's special-case for str
  once the general String representation is first-class.

## Non-goals

* Changing JBMC's string handling. JBMC uses refined strings and
  we keep that path exactly as-is.
* Changing C/C++ frontends' treatment of strings. They stay
  on the C array-of-char model.
* Supporting SMT strings on non-CVC5/Z3 backends. The plan is
  specifically to benefit SMT backends that support the string
  theory, which in practice means CVC5 first, then Z3.

## Risks

* **JBMC regression surface**. Even with the refined-string path
  unchanged, the refactor will touch shared code. We'll need to
  guard with full JBMC regression runs after each phase.
* **Intrinsic completeness**. We may find Python string
  operations that don't have an intrinsic today; defining a new
  intrinsic is cheap but each requires both back-end
  implementations. Worth auditing in Phase 1.
* **Performance**. SMT-LIB string theory solvers are typically
  slower than bitvector reasoning for simple operations. We'll
  need benchmarks to compare before committing to SMT strings as
  the default for `--cvc5` on Python.

## Proposed size of work

A ballpark estimate based on Phase 3's inventory: **~two weeks of
focused work**, split roughly 30 % inventory/design, 40 %
frontend refactor, 20 % back-end implementation, 10 %
benchmarking and regression-run cleanup. The work is naturally
broken into PRs — one per phase.

## Relationship to Wave 2

The Wave 2 SMT-regex intrinsics landed before this refactor.
Their current state:

* `cprover_string_match_func` / `_search_func` / `_fullmatch_func`
  are declared, intercepted in `smt2_conv`, and work for
  string-literal subjects (the interception substitutes the
  literal into the SMT regex term).
* A direct user call such as
  `__cbmc_re_match("abc", "abcxyz")` verifies precisely under
  `--cvc5`.
* The library wrapper `re.match(pattern, subject)` still uses
  parameter indirection: when user code calls
  `re.match("abc", "abcxyz")`, the args become parameter symbols
  inside the library function body, and the intrinsic — invoked
  *inside* that body — sees symbols, not literals. The SMT
  interception can't substitute. Today this means
  `re.match("abc", "abcxyz")` returns the conservative nondet.

Once this plan lands, the subject reaches the SMT solver as a
`String` regardless of SSA indirection, and `re.match("abc",
subject)` becomes precise even for symbolic subject — which is
the real user-facing win of Wave 2.

## Review request

Please review — in particular Phase 1's inventory strategy and
the back-end abstraction in Phase 2 — before we start building.
