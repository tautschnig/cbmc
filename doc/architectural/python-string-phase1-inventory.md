\file
# (a') Phase 1 inventory — Python-string representation refactor

Status: **baseline inventory**, May 2026. This is the
deliverable of Phase 1 of the plan in
`python-string-representation-plan.md`: a per-site catalog of
every place the Python front-end reaches into the refined-string
struct (`{length, data}`). Each site is classified by role
(Producer, Consumer, Mutator) and given a target intrinsic that
will replace the direct struct manipulation in Phase 3.

Numbers throughout refer to `src/python/python_converter.cpp` at
commit HEAD of this tree.

## Counting

Before we drill in:

* `struct_exprt{..., python_string_type()}` — **5 direct
  constructions**, plus **33 calls** to the
  `build_string_struct` helper that wraps them. All Producers.
* `member_exprt{..., "length", …}` — **17 sites**, roughly
  split Consumer (len, truthiness, iteration bounds) and
  Mutator (set length on a freshly built string).
* `member_exprt{..., "data", …}` — **7 sites**, all Consumer
  or Mutator (indexing the data array, copying bytes).
* `cprover_string_*_func` calls — **10 sites**, already on
  the target path. Listed here for completeness so Phase 3 has
  a single catalogue.
* `emit_string_bool_function` call sites — **7 sites**, all
  thin wrappers that construct a `cprover_string_*_func`
  application. No direct struct manipulation.

Total distinct sites touching the representation: **~50**.

## Producer sites (construct a string)

| Where | What | Target |
| --- | --- | --- |
| `build_string_struct` (definition + 33 callers) | String literal. Builds an inline `struct_exprt{length, address_of(array[0])}`. | Replace with `cprover_string_literal_func("…")` — the back-end maps to either a refined-string struct or an SMT `String`. |
| `python_convertert::build_string_literal` | Same, but with persistent storage. Currently unused in Phase 0. | Merge into `cprover_string_literal_func` once back-ends implement it with persistent storage where needed. |
| `convert_call` → BinOp Add → ~line 2042 | String `+` String. Writes `.length = l+r` and loops copying bytes. | `cprover_string_concat_func`. |
| `convert_call` → BinOp Mult → ~line 2181 | String `*` int. Writes `.length = l*n` and loops. | New: `cprover_string_repeat_func(str, n)` — not yet in the refined-string intrinsic set; add. |
| `@c_intrinsic` return marshalling → ~line 8420 | Wrap a `char *` from C into a refined-string struct with nondet length. | Intrinsic won't help; this is specifically a Python-layer shim over C. Keep struct construction here — the interface boundary is the only legitimate place it survives. |
| `ast.copy_location(...)` equivalent for f-strings → ~line 4159 | Build list of substrings. | Indirect via `build_string_struct` — covered. |

## Consumer sites (inspect a string)

| Where | What | Target |
| --- | --- | --- |
| `truthiness` → line 989 | `bool(s)` lowers to `s.length != 0`. | `cprover_string_is_empty_func` exists — use it (with `not_exprt{…}`). |
| `BinOp Eq/NotEq` → ~lines 3147, 3218 | Already via `cprover_string_equal_func`. | Already on target path. |
| `Compare Lt/LtE/Gt/GtE` → ~line 3060 | Reads `left.data[0]` and `right.data[0]` and compares bytes. Not a real string comparison. | Replace with `cprover_string_compare_func` (new — exists in the Java-side, add to Python). |
| `In operator` (`c in s`) → ~line 3385 | Uses `cprover_string_contains_func` — already on target path. | Already covered. |
| `in for single char` fallback → ~line 3403 | Reads `item.data[0]`. Redundant once the char path goes through `cprover_string_char_at_func`. | `cprover_string_char_at_func`. |
| `upper/lower` → ~lines 4346, 4372 | Already via intrinsics. | Already covered. |
| `startswith/endswith` → ~line 5006 | Already via intrinsics. | Already covered. |
| `list.pop/delete` updating `.length` → ~line 5462, 5511, 5540, 5599 | These are list sites, not string — mis-fired grep. No action. | n/a |
| `convert_expression` BuiltIn `chr(cp)` → ~line 6473 | Constructs a fresh struct with the UTF-8 byte sequence. | `cprover_string_of_int_func` or a new `cprover_string_chr_func`; the existing refined-string intrinsic set handles `String.valueOf(int)` for Java — extend. |
| `len(s)` → ~line 6228, 6249 | Reads `.length`. | `cprover_string_length_func`. |
| `str(x)` / `repr(x)` various paths → ~lines 1482, 1701, 1736, 1927, 2091, 4268, 4558, 4641, 4753, 5139, 5469, 5622, 6417, 6526, 7097, 7099, 7146, 7156, 7172, 7177, 8191, 8639, 8662 | All compile-time constant folds that build a literal via `build_string_struct`. | Covered by Producer case. |
| `convert_call` → `__cbmc_re_*` → ~line 5856 | Projects to `{length, data}` struct for a non-struct argument. | Replace with explicit pattern/subject argument — the intrinsic already uses `(ite …)` in `smt2_conv`. Simply pass the SMT-level string value. |
| iteration `for c in s` → ~line 11360 | Builds `c = {1, address_of(&s.data[idx])}`. | `cprover_string_char_at_func` returning a `(String(length=1, content=⟨char⟩))`. |

## Mutator sites (write to a string's fields)

Mutators are rare in Python (strings are immutable) but occur
where we build a new result struct via field assignments:

| Where | What | Target |
| --- | --- | --- |
| BinOp Add / Mult → lines 2042, 2181, 2280 | Assigning `.length` and filling `.data[i]` in a loop. | `cprover_string_concat_func` / `cprover_string_repeat_func` — a single intrinsic call replaces the loop. |
| upper/lower → line 4372 | Same shape. | Replaced by the existing case transformation intrinsics; already on target path. |
| @c_intrinsic return wrapping → line 8420 | Fills nondet length. | Stay — interface boundary. |

## Existing intrinsic coverage

Intrinsics already callable from the front-end today:

* `cprover_string_literal_func` — Java/refined-strings has it;
  Python front-end doesn't yet call it.
* `cprover_string_equal_func` — in use on `==` and `!=`.
* `cprover_string_length_func` — available; front-end uses
  direct `.length` member access instead.
* `cprover_string_concat_func` — in use.
* `cprover_string_char_at_func` — available; not used.
* `cprover_string_is_empty_func` — available; not used.
* `cprover_string_contains_func` — in use for `in`.
* `cprover_string_is_prefix_func` — in use for `startswith`.
* `cprover_string_is_suffix_func` — in use for `endswith`.
* `cprover_string_index_of_func` / `..._last_index_of_func` —
  available; Python's `find/rfind/index/rindex` use scratch
  loops today.
* `cprover_string_to_upper_case_func` /
  `..._to_lower_case_func` — in use.
* `cprover_string_{match,search,fullmatch}_func` — Wave 2
  additions, in use on the (b) code path.

## Intrinsics not yet available (would need to be added)

| Intrinsic | Used by |
| --- | --- |
| `cprover_string_repeat_func(s, n)` | `s * n` |
| `cprover_string_compare_func(a, b)` | `a < b`, `a > b`, `a >= b`, `a <= b` |
| `cprover_string_chr_func(cp)` | `chr(cp)` — can alternatively be implemented as `cprover_string_of_int_func` on the low byte plus UTF-8 encoding; see Phase 4 discussion. |
| `cprover_string_substring_func(s, start, end)` | Python slicing `s[a:b]`. Java/JBMC has this; front-end isn't wired up. |
| `cprover_string_index_of_from_func` | `s.find(substr, start)` — a variant of the existing `index_of` with a start offset. |
| `cprover_string_replace_func(s, old, new)` | `s.replace(a, b)` — handled today by a scratch loop. |
| `cprover_string_split_func` | `s.split(sep)` — more complex; may not be in Phase 3 scope. |
| `cprover_string_strip_func` / `..._lstrip_func` / `..._rstrip_func` | `s.strip()` etc. Cheap to add. |
| `cprover_string_trim_func` (alias) | JBMC has a variant. |
| `cprover_string_endswith_from_func` / `..._startswith_from_func` | `s.endswith(suffix, start, end)`. |

## Phase 3 ordering (proposed)

Execute in this order, one PR per bullet:

1. **Replace `build_string_struct` with `cprover_string_literal_func`**
   — producer side. Largest single reduction in struct-touching
   sites (33 → 0 direct callers). Back-end glue (refine-strings
   keeps current encoding; SMT backend lowers to `(str.=)` /
   `String` literal).

2. **Replace `.length` reads with `cprover_string_length_func`**.
   Mechanical. Hits the truthiness site, `len(s)`, iteration
   bounds.

3. **Replace `.data[i]` reads and scratch-loop comparisons with
   `cprover_string_char_at_func` and `cprover_string_compare_func`**.
   Needs the new `compare` intrinsic; add axioms in
   refine-strings.

4. **Replace the concat/repeat loop producers with
   `cprover_string_concat_func` / `..._repeat_func`**. Needs
   `repeat` added to the intrinsic set.

5. **Migrate substring, replace, split, strip, find-from**.
   Each independent; land individually.

6. **Re-encode `@c_intrinsic` return wrapping** using whatever
   shape the back-end chooses. If SMT strings are the chosen
   representation on `--cvc5`, the return wrapping can drop the
   refined-string struct entirely and use a direct `String` value.

After Phase 3 steps 1–5, the front-end has **zero direct
`.length`/`.data` accesses** except the
interface-boundary cases that `@c_intrinsic` requires. That's
the state we want before Phase 4 (back-end selector) and Phase
5 (remove legacy shims).

## Risks / open questions

* **Substring / replace / split axiom coverage** — the
  refined-string backend already has partial coverage for Java
  use. Phase 3.5 needs a gap analysis (likely a short
  regression-driven investigation). Unknown how much work.
* **UTF-8 representation mismatch** — Python strings are
  code-point oriented but the refined-string backend operates
  on bytes. SMT-LIB's `String` is Unicode-code-point-oriented,
  so the refactor may actually *help* here by dropping the
  byte-level view. Phase 2 must decide whether the front-end
  normalises to UTF-8 before handing to intrinsics, or relies
  on the back-end to handle Unicode.
* **Performance** — if SMT strings are slower than
  bitvector-struct encoding on simple programs, we may want
  the back-end to retain the struct representation even on
  `--cvc5`. The abstraction in Phase 2 accommodates this.
* **Intrinsic signature churn** — every new intrinsic is two
  places (irep_ids.def and `string_constraint_generator*`) at
  minimum, plus an entry in `smt2_conv.cpp` for the SMT path.
  Group the additions into one PR at Phase 3 start.

## Output

This document is the deliverable of Phase 1. Sign-off on this
catalog is the gate to starting Phase 2 (back-end abstraction
design). Open questions above should be addressed during
Phase 2 design review.
