# Refined String Solver Migration Plan for TypeScript Frontend

Status: Phase 1 + Phase 10 done; Phases 2–8 deferred. See
"Execution status" section.
Model: the Python frontend's migration on branch `tautschnig/py`  
Estimated effort: ~500 LOC in our frontend + ~20 LOC in CBMC solver/ + ~50 test adjustments

This document describes a detailed step-by-step plan for adopting
CBMC's refined string solver (`src/solvers/strings/`) in the
TypeScript frontend. The plan is derived from studying the Python
frontend's migration on the `tautschnig/py` branch (commits from
`5555d73df0` through `7228ef8db5`).

## Execution status (as of 2026-05-10)

- **Phase 1 (type retag)**: DONE. Struct keeps inline-array shape,
  only the tag changed to `CPROVER_PREFIX "refined_string_type"`.
  The plan called for switching data to a pointer; we chose a
  safer minimal change that keeps all 626 CORE tests passing.
- **Phase 10 (auto-enable)**: DONE. `.ts`/`.tsx` source triggers
  `--refine-strings` automatically unless `--z3`/`--smt2` is set.
- **Phases 2–8 (handler migration)**: NOT DONE. Deferred because:
  - All 4 originally-targeted string KNOWNBUGs were already
    closed in the previous session via per-case symbolic
    encodings (see `doc/over-approximation-audit.md`).
  - Full migration requires switching our inline-array struct to
    a pointer-based `refined_string_exprt`, which is invasive
    (~500 LOC) and risks destabilising the 626 existing CORE
    tests.
  - The refined-string solver now *recognises* our strings (via
    the retagged type), but doesn't yet *constrain* any of our
    string operations because our handlers emit struct literals
    rather than `cprover_string_*_func` applications.
- **Phase 9 (solver patches)**: NOT NEEDED yet; the solver
  accepts our shape with the retagged type without the patches
  described in the plan.

## Future work

### Phase 1 pilot (attempted 2026-05-10, rolled back)

An attempt was made to complete Phase 1 per the original plan:
switch `typescript_string_type()` to the pointer-based shape
(`{length: signedbv[32], content: pointer<unsignedbv[16]>}`),
update `convert_string_literal_from_text` to store characters in
a fresh static symbol and produce `address_of(index(sym, 0))` as
the content pointer, and update `extract_string_value` to read
both legacy inline-array and new pointer-based formats.

Compilation succeeded but regression revealed **~25 test
failures** covering:

- `map-set-operations`, `map-delete-clear`, `map-get-constant`,
  `map-set-symbolic` — map key lookup compares stored string
  keys to the query key
- `generic-*`, `heterogeneous-tuple`, `discriminated-union` —
  tests that use string members as union discriminators or tuple
  fields
- `integration-config-parser`, `integration-ms-parser` — real
  TS program integrations
- `typeof-literal-types` — typeof-guard string-literal types
- `object-entries-deep-access` — Object.entries result iteration
- Plus several more

**Root cause**: under the pointer-based shape, struct equality
(used implicitly by `===` and other operators that compare whole
strings) degenerates to pointer comparison. Each string literal
gets its own fresh symbol, so `"a" === "a"` compares distinct
pointers and returns false even when the characters match. To
fix this properly, every string operation that implicitly
compares strings must be rewritten to emit
`cprover_string_equal_func(a, b)` (or a per-character loop).

That means Phase 1 can't ship without simultaneous completion of
Phase 4 (predicates) — and since predicates depend on Phase 2
(helpers) and Phase 3 (concat), the phases aren't really
independent. In practice they form a single ~500 LOC migration.

The pilot was rolled back. We remain on Phase 1 minimum (retag
only) + Phase 10 (auto-enable).

### If we resume the migration

Two approaches:

- **Boundary translation** (as before): keep inline-array struct,
  wrap into `refined_string_exprt` at call sites to
  `cprover_string_*_func`. Simpler but the inverse (solver
  result → our struct shape) is awkward because it needs a
  per-character loop.
- **Full pointer-based switch** (what the pilot tried): change
  the struct shape, emit `cprover_string_equal_func` for `===`,
  `cprover_string_concat_func` for `+`, etc. Most rigorous but
  requires migrating ~20 method handlers at once.

Either way, the work is best done as a dedicated multi-session
effort with per-method test triage, not a single-session
attempt.

### Why this is lower priority now

All 4 originally-targeted string KNOWNBUGs
(`string-repeat-symbolic`, `string-padstart-symbolic`,
`string-indexof-symbolic-needle`, `string-to-number-coerce`)
were closed in an earlier session via per-case symbolic
encodings using our existing fixed-size model (see
`doc/over-approximation-audit.md`). The only remaining string
KNOWNBUG that would be resolved by full migration is
`string-to-number-coerce-symbolic` (symbolic string parse),
which is an edge case rarely encountered in real TS code.

---



The Python frontend successfully migrated. Key insights:

1. **No separate string type** — the Python frontend reused its own
   `python_str` struct but renamed the tag to
   `__CPROVER_refined_string_type` (so `is_refined_string_type()`
   returns true) AND changed the data field from array to pointer.

2. **Two solver patches needed** (both small): `extract_strings` in
   `string_refinement.cpp`, and `get_string_expr` in `array_pool.cpp`.
   These are general-purpose fixes; upstream may already have them.

3. **The string solver is opt-in via `--refine-strings`** — the
   frontend auto-enables it for its own source files.

4. **Each string method becomes a `function_application_exprt`** to
   `cprover_string_*_func` with a result struct created by a helper.
   About 20 method handlers need rewriting.

5. **Gotchas** (documented in Python commit messages):
   - Non-struct string expressions crash `get_string_expr` (fixed in solver)
   - `pending_checks` must flush before branches (emit order matters)
   - Equality needs `c_bool_type`, not `bool_typet` (SSA biconditional)
   - String solver conflicts with `--z3`/`--smt2` backends (disable in those cases)

## Scope

After migration, the following currently-KNOWNBUG tests should
become CORE:

- `string-repeat-symbolic` (symbolic count)
- `string-padstart-symbolic` (symbolic target length)
- `string-indexof-symbolic-needle`
- `string-to-number-coerce` (`+"42"`)

Plus the currently-documented limitation "String concatenation with
parameter-typed strings" gets a correct symbolic encoding.

## Step-by-step plan

### Phase 1: Migrate the string type (isolated refactor)

**Goal**: change `typescript_string_type()` to pointer-based, retag
as refined_string_type. All existing constant-string paths still work.

**Files**:
- `src/typescript/typescript_types.h`: struct definition
- `src/typescript/typescript_converter.cpp`: `build_string_struct`,
  `extract_string_value`, all direct array-based string construction
- `src/typescript/typescript_converter_call.cpp`: callsites that
  construct `{length, array_of_chars}`

**Changes**:

```cpp
// typescript_types.h
#include <util/cprover_prefix.h>
#include <util/pointer_expr.h>
#include <util/refined_string_type.h>
#include <util/string_expr.h>

inline struct_typet typescript_string_type()
{
  struct_typet::componentst components;
  components.push_back(struct_typet::componentt{"length", signedbv_typet{64}});
  components.push_back(struct_typet::componentt{
    "data", pointer_typet(unsignedbv_typet{16}, 64)});  // 16-bit for UTF-16
  struct_typet result(components);
  result.set_tag(CPROVER_PREFIX "refined_string_type");
  return result;
}

inline bool is_typescript_string_type(const typet &type)
{
  return is_refined_string_type(type);
}
```

Note: length is `signedbv[64]` (was `signedbv[32]`) to match refined string
solver expectations.

**Literal construction** (new `build_string_struct`):

```cpp
static exprt build_string_struct(const std::string &s)
{
  exprt::operandst chars;
  for(char c : s)
    chars.push_back(
      from_integer(static_cast<unsigned char>(c), unsignedbv_typet{16}));
  if(chars.empty())
    chars.push_back(from_integer(0, unsignedbv_typet{16}));
  array_typet at(unsignedbv_typet{16},
                 from_integer(chars.size(), signedbv_typet{64}));
  array_exprt arr(std::move(chars), at);
  exprt content = address_of_exprt(
    index_exprt(arr, from_integer(0, signedbv_typet{64}), unsignedbv_typet{16}));
  exprt length = from_integer(static_cast<long long>(s.size()), signedbv_typet{64});
  return struct_exprt({length, content}, typescript_string_type());
}
```

**Read-back** (`extract_string_value` must handle BOTH the new
pointer-based literal format AND the old array format for backward
compatibility during the migration):

```cpp
// In extract_string_value, when the expression is a struct_exprt:
const exprt &data_op = e.operands()[1];
const exprt *arr = nullptr;
// New format: operands()[1] is address_of(index(array, 0))
if(data_op.id() == ID_address_of &&
   data_op.operands().size() == 1 &&
   data_op.operands()[0].id() == ID_index)
  arr = &data_op.operands()[0].operands()[0];
// Legacy format: operands()[1] is array directly
if(data_op.id() == ID_array)
  arr = &data_op;
// ... extract chars from arr as before
```

**Expected fallout**: ~20 string-method handlers will build
`struct_exprt{{length, array_exprt}, typescript_string_type()}` —
this will now fail because the type says pointer. Temporarily mark
those tests as KNOWNBUG with `TODO: migrate to cprover_string_*_func`.

**Deliverable for Phase 1**: `typescript_string_type()` is
refined-string-compatible. Constant-string literals, length access,
and simple comparison still work. 20–25 tests move to KNOWNBUG
pending Phase 2.

**Commit per**: one commit for the type change + legacy fallback,
one commit per batch of tests moving to KNOWNBUG.

### Phase 2: Add helpers for emitting string function applications

**Goal**: two helper functions for emitting
`cprover_string_*_func` calls. Used by all subsequent method
migrations.

**New file or addition**: `src/typescript/typescript_converter_call.cpp`.

```cpp
static exprt make_nondet_string(symbol_table_baset &symbol_table)
{
  static unsigned str_ctr = 0;
  std::string len_name = "__string_len_" + std::to_string(str_ctr);
  std::string ptr_name = "__string_ptr_" + std::to_string(str_ctr);
  str_ctr++;

  irep_idt len_id{"typescript::" + len_name};
  if(symbol_table.lookup(len_id) == nullptr)
  {
    symbolt ls{len_id, signedbv_typet{64}, "typescript"};
    ls.base_name = len_name;
    ls.is_lvalue = true;
    ls.is_state_var = true;
    symbol_table.add(ls);
  }

  irep_idt ptr_id{"typescript::" + ptr_name};
  if(symbol_table.lookup(ptr_id) == nullptr)
  {
    symbolt ps{ptr_id, pointer_typet(unsignedbv_typet{16}, 64), "typescript"};
    ps.base_name = ptr_name;
    ps.is_lvalue = true;
    ps.is_state_var = true;
    symbol_table.add(ps);
  }

  return struct_exprt(
    {symbol_table.lookup_ref(len_id).symbol_expr(),
     symbol_table.lookup_ref(ptr_id).symbol_expr()},
    typescript_string_type());
}

static exprt emit_string_function(
  const irep_idt &func_id,
  const exprt::operandst &extra_args,
  symbol_table_baset &symbol_table,
  std::vector<codet> &pending_stmts)
{
  exprt result = make_nondet_string(symbol_table);

  // Declare the function in the symbol table
  std::vector<typet> arg_types;
  arg_types.push_back(signedbv_typet{64});                      // result length
  arg_types.push_back(pointer_typet(unsignedbv_typet{16}, 64)); // result content
  for(const auto &a : extra_args)
    arg_types.push_back(a.type());

  irep_idt sym_id{func_id};
  if(symbol_table.lookup(sym_id) == nullptr)
  {
    symbolt fs{
      sym_id,
      mathematical_function_typet(std::move(arg_types), signedbv_typet{32}),
      "typescript"};
    fs.base_name = id2string(func_id);
    symbol_table.add(fs);
  }

  exprt::operandst args;
  args.push_back(result.operands()[0]);  // length
  args.push_back(result.operands()[1]);  // content
  args.insert(args.end(), extra_args.begin(), extra_args.end());

  function_application_exprt app(
    symbol_table.lookup_ref(sym_id).symbol_expr(), args);
  app.type() = signedbv_typet{32};

  // Assign return code (we ignore it but the solver needs it)
  static unsigned rc_ctr = 0;
  std::string rc_name = "__str_rc_" + std::to_string(rc_ctr++);
  irep_idt rc_id{"typescript::" + rc_name};
  if(symbol_table.lookup(rc_id) == nullptr)
  {
    symbolt rs{rc_id, signedbv_typet{32}, "typescript"};
    rs.base_name = rc_name;
    rs.is_lvalue = true;
    rs.is_state_var = true;
    symbol_table.add(rs);
  }
  pending_stmts.push_back(
    code_frontend_assignt{symbol_table.lookup_ref(rc_id).symbol_expr(), app});

  return result;
}

// For boolean predicates (equal, contains, is_prefix/suffix)
static exprt emit_string_bool_function(
  const irep_idt &func_id,
  const exprt &str1,
  const exprt &str2,
  symbol_table_baset &symbol_table,
  std::vector<codet> &pending_stmts);
```

**Deliverable**: helpers compile, no behavior change yet.

### Phase 3: Migrate concat (simplest method)

**Goal**: string `+` and `.concat()` use `cprover_string_concat_func`
for non-constant operands. Constants still use the synthesized literal.

**Key helper**: decompose symbol/member expressions into struct_exprt
(the solver requires struct_exprt as input):

```cpp
auto to_string_struct = [](const exprt &s) -> exprt {
  if(s.id() == ID_struct && s.operands().size() == 2)
    return s;
  return struct_exprt(
    {member_exprt(s, "length", signedbv_typet{64}),
     member_exprt(s, "data", pointer_typet(unsignedbv_typet{16}, 64))},
    s.type());
};
```

**Handler change** (in the `+` operator, String.concat method):

```cpp
// Old: return side_effect_expr_nondett{typescript_string_type(), ...};
// New:
return emit_string_function(
  ID_cprover_string_concat_func,
  {to_string_struct(left), to_string_struct(right)},
  symbol_table, pending_stmts);
```

**Deliverable**: symbolic string concat works (promote tests).

### Phase 4: Migrate string predicates

Handlers for `includes`, `startsWith`, `endsWith`, `===`/`==`/`!==`/`!==`
(for strings). Each becomes a `cprover_string_*_func` call via
`emit_string_bool_function`.

```cpp
// String equality:
auto eq_str = emit_string_bool_function(
  ID_cprover_string_equal_func,
  to_string_struct(left), to_string_struct(right),
  symbol_table, pending_stmts);
// Note from Python: use c_bool_type, not bool_typet, to avoid
// SSA biconditional issues.

// includes:
auto contains = emit_string_bool_function(
  ID_cprover_string_contains_func,
  to_string_struct(haystack), to_string_struct(needle),
  symbol_table, pending_stmts);

// startsWith / endsWith:
auto prefix = emit_string_bool_function(
  method == "startsWith" ? ID_cprover_string_is_prefix_func
                         : ID_cprover_string_is_suffix_func,
  to_string_struct(haystack), to_string_struct(prefix),
  symbol_table, pending_stmts);
```

### Phase 5: Migrate single-argument transformations

`toLowerCase`, `toUpperCase`, `trim`:

```cpp
return emit_string_function(
  ID_cprover_string_to_lower_case_func,  // or _upper_case_func
  {to_string_struct(receiver)},
  symbol_table, pending_stmts);
```

### Phase 6: Migrate indexed operations

`substring` / `slice` (take int args): `cprover_string_substring_func`
with length and content of source plus start/end indices.

`indexOf` / `lastIndexOf`: `cprover_string_index_of_func` /
`cprover_string_last_index_of_func` — these RETURN an int, not a
string, so use a different pattern:

```cpp
function_application_exprt app(
  symbol_table.lookup_ref(ID_cprover_string_index_of_func).symbol_expr(),
  {to_string_struct(haystack), to_string_struct(needle), fromIndex});
app.type() = signedbv_typet{32};
// Assign to a fresh symbol and return that symbol.
```

### Phase 7: Variable-length construction

`repeat`, `padStart`, `padEnd`: these are harder because the result
length depends on the input. The solver supports them via
`set_length`, `concat_char`, etc. Pattern:

```cpp
// s.repeat(n): repeatedly concat
exprt acc = make_nondet_string(symbol_table);
// Set acc = empty via literal
// Loop n times: acc = concat(acc, s)
// Emit as bounded loop with cprover_string_concat_char_func per iter
```

For truly symbolic count, this may not be fully expressible —
document as a limitation if needed.

### Phase 8: Number ↔ String

`Number.parseInt` / `parseFloat`: use
`cprover_string_parse_int_func`.

`(x).toString()`: use `cprover_string_of_long_func` (for integer-
valued doubles) or `cprover_string_of_double_func`.

`"42" + 0` coercion: detect in binary-op handler, coerce with
`cprover_string_parse_int_func`.

### Phase 9: Solver-side patches

Two changes in `src/solvers/strings/`:

**1. `string_refinement.cpp` — `extract_strings` / `extract_strings_from_lhs`**:

```cpp
// Before:
if(lhs.type() == string_typet()) { ... }

// After:
if(lhs.type() == string_typet() || is_refined_string_type(lhs.type())) { ... }
```

Do the same in `extract_strings`.

**2. `array_pool.cpp` — `get_string_expr`**:

Handle non-struct expressions by decomposing via member_exprt:

```cpp
if(expr.id() == ID_struct && expr.operands().size() == 2)
{
  const refined_string_exprt &str = to_string_expr(expr);
  return array_pool.find(str.content(), str.length());
}
// For non-struct expressions (symbols, member_exprt), decompose.
const auto &st = to_struct_type(expr.type());
PRECONDITION(st.components().size() == 2);
exprt length = member_exprt(expr, st.components()[0].get_name(),
                            st.components()[0].type());
exprt content = member_exprt(expr, st.components()[1].get_name(),
                             st.components()[1].type());
return array_pool.find(content, length);
```

Both changes are self-contained and small (~10 LOC each). They
should eventually be upstreamed — they benefit any frontend using
refined strings.

### Phase 10: Driver — auto-enable --refine-strings

In `src/cbmc/cbmc_parse_options.cpp`, where the TypeScript frontend
is detected (probably in the language-mode switch):

```cpp
// For .ts/.tsx files, auto-enable the string solver unless the
// user explicitly chose a conflicting backend.
if(is_typescript_source)
{
  if(!cmdline.isset("z3") && !cmdline.isset("smt2"))
    options.set_option("refine-strings", true);
}
```

**Warning (from Python)**: auto-enabling --refine-strings caused
spurious failures in non-string tests like binary_search. The Python
branch reverted auto-enable and went back to explicit `--refine-strings`
when needed. We should watch for this and be prepared to revert.

### Phase 11: Flush pending_stmts at branch points

`cprover_string_*_func` calls are emitted as assignments to
pending_stmts. If a string operation appears inside an `if` condition
or loop test, the pending assignment must be emitted BEFORE the
branch. Without this, the solver doesn't see the constraint on the
expected path.

Python's commit `7183ef26f7` added a pending_checks flush in
`convert_if`. We'd need the same in `convert_expression` for
`if-then-else`-like patterns, and possibly in loop conditions.

### Phase 12: Address gotchas one-by-one

Each gotcha the Python branch encountered will likely recur:

- **Symbol value tracking through function returns**: make sure
  string_refinement's `extract_strings` sees the arguments of
  function returns. May need additional patches.
- **`is_lvalue` / `is_state_var` flags on temp symbols**: the
  solver is picky about this.
- **`c_bool_type` vs `bool_typet` for equality results**.
- **Conversion-time constant folding**: check that
  `build_string_struct("foo") + build_string_struct("bar")` still
  folds to the literal "foobar" rather than always calling the
  solver.
- **Type-tag vs type-struct equality**: the solver uses
  `is_refined_string_type()` via the tag; any code that compares
  types via `==` may break.

### Phase 13: Re-enable moved-to-KNOWNBUG tests

As each phase lands, move affected tests back from KNOWNBUG to CORE.

## Milestones and test counts

| Phase | Migrated | KNOWNBUG delta |
|-------|----------|----------------|
| 1: type migration | — | +20–25 (tests break) |
| 2: helpers | — | 0 |
| 3: concat | 5 tests | −5 |
| 4: predicates | 10 tests | −10 |
| 5: transforms | 5 tests | −5 |
| 6: indexed ops | 8 tests | −8 |
| 7: variable-length | 2–4 (repeat, padStart, padEnd) | −2–4 |
| 8: number↔string | 3 tests | −3 |
| 9: solver patches | — | (enables above) |
| 10: driver | — | 0 |
| 11–13: gotchas | TBD | TBD |

After completion: the 4 string KNOWNBUGs documented in
`doc/over-approximation-audit.md` should all flip to CORE, plus
many of the tests we had to move to KNOWNBUG in Phase 1 should
come back.

## Alternative: keep fixed-size model, add per-case symbolic encodings

An alternative to full refined-string migration is to add per-case
symbolic encodings for each remaining KNOWNBUG using our existing
fixed-size model:

- `repeat(n)`: per-slot `if_exprt` chain over possible n values
- `padStart(n)`: similar
- `indexOf(needle)` with symbolic needle: nested match loop

This avoids the refactor but creates specific per-case code that
doesn't generalize. The refined-string migration is the better
long-term solution because it reuses CBMC's existing solver.

## Risk assessment

**Low risk**:
- Type migration (Phase 1) — localized, test-guarded
- Helper functions (Phase 2) — pure additions
- Concat migration (Phase 3) — single method
- Solver patches (Phase 9) — well-understood, small

**Medium risk**:
- Predicate migration (Phase 4) — equality handling is subtle
- Indexed operations (Phase 6) — bounds and negative indices
- Pending-stmts flush points (Phase 11) — easy to miss one

**High risk**:
- Auto-enabling `--refine-strings` (Phase 10) — may break non-string tests
- Unknown solver interactions with our tuple/union/class types

## Recommended implementation order

1. Read the Python commits `5555d73df0`, `420de5514c`, `02eb227907`,
   `f964eecf26`, `7183ef26f7`, `23aaf85370`, `ad9c5752a7` to get
   the full feel.
2. Create a branch `typescript-refined-strings`.
3. Execute Phase 1 + Phase 9 solver patches in parallel (both small,
   independent).
4. Execute Phase 2, 3 (concat) as the first end-to-end test.
5. Run full regression after each phase. Every broken test either
   becomes KNOWNBUG (during migration) or gets fixed.
6. Iterate through remaining phases; each yields multiple tests
   flipping from KNOWNBUG to CORE.
7. Phase 10 (auto-enable) last, after the other phases prove stable.

Estimated total: 2–4 days of focused work following this plan.
