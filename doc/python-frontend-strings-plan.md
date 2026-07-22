# Python frontend — strings & regex plan

This is **the** forward-looking plan for all `str`/`bytes` and `re`
(regex) work in the Python frontend. Everything *not* string-related
lives in the companion [Python frontend plan](python-frontend-plan.md);
the [architecture document](python-frontend-architecture.md) is the
authoritative reference and its gaps table links here for every
string/regex gap.

## Status (2026-06-19)

- **Native SMT-LIB String backend: COMPLETE** (Plan A, 2026-06-12).
  `--python-smt-strings` (with `--cvc5`/`--z3`) selects a native SMT-LIB
  `String` representation end-to-end (the byte-array+`str` hybrid was
  retired). The full SMT-theory string surface is precise and fast there:
  `==`/`!=`, ordering (`<`/`<=`/…, which is the refined-backend *ceiling*),
  `len`, `in`/`not in`, `startswith`/`endswith`, `find`/`index`, `+`/concat,
  subscript, slice, `replace`, the `strip` family (via SMT-LIB regex),
  f-strings (incl. `str(int)`/`chr`/`ord`), with model extraction. The
  `regression/python` corpus is 540/540 with a verdict under native.
- **Refined-string backend is the no-external-solver default.** Its
  cleanly-achievable precision wins have landed (`rfind`/`rindex`,
  Python-whitespace `strip`); the remaining refined gaps are
  characterised below and are **answered by the native opt-in**.
- **`str(int)` is exact for large integers** (no `double`-folding;
  `2026-06-18`).
- **No string false proofs** — the 2026-06-11 soundness audit found none;
  the one flagged case (`string-nondet-in-embedded-null-longer-fail`) is
  *vacuously sound*.

## Remaining string/regex gaps (all sound; precision/perf)

| Gap | Backend | Status / route |
|---|---|---|
| ordering `< <= > >=` (symbolic) | refined | precise on **native**; refined needs existential-witness instantiation ([§4](#regex), research) |
| substring `replace` (multi-char old/new) | refined | precise on **native** (`str.replace_all`); refined axiom is char-only |
| `split` (list-valued result) | both | hard everywhere (variable-count result); native residual. **Count bounds landed 2026-06-22** (`3006ccab3d`): the native symbolic-subject fallback now constrains the segment count to `>= 1` (explicit separator) and `<= len(s)+1` (sep length >= 1), so `len(s.split(d)) >= 1` / `<= len(s)+1` no longer false-alarm. The full segment *partition* (reconstructing `s` from the segments) stays the hard residual; `rsplit` still uses the looser path. |
| `casefold`/`title` (Unicode case-mapping) | native | no SMT-LIB primitive → bounded encoding or refined; sound-nondet today |
| `count` (symbolic) | refined | needs a counting axiom |
| membership over a symbolic-length *produced* needle | refined | converges in practice; bounded guard; precise on native |
| negated-regex membership in multi-assert (`re4`/`re11`) | refined | slow/timeout; native precise; presence-based `Match` truthiness is the recovery |
| regex literal-symbolic patterns, deep refined regex axioms | refined | fold into the native backend |
| `complex(<non-literal string>)` parse (`"5+6j"`→(5,6)) | both | needs runtime string→number parsing; literal strings already fold. Moved here from the complex cluster (it is string-parsing, not complex arithmetic). The remaining 2 failing asserts in `complex_constructor_extended`. |
| **native `nondet_string(N)` length** | native | **FIXED 2026-06-22** — the native path ignored the size arg (length only bounded to `[0,MAX]`, so `s` could be `""`), spuriously FAILing length-dependent asserts that pass on refined (`string-nondet-length-success`). Now constrains `len == N` (`string-smt-native-nondet-length`). |
| **native `== ""` (empty-string literal)** | native | **FIXED 2026-06-22** (`54041af9ca`). Symptom: `nondet_string(0) == ""` reported a spurious `VERIFICATION ERROR` ("line 2: unexpected token" / "non-Boolean value for B0"); `!= ""`, `"" == ""`, `"x" == ""`, and `nondet_string(N>0) == ""` were all fine. **True root cause:** the native string length is read back as `((_ int2bv 64) (str.len s))`, but `str.len` is an unbounded non-negative SMT Int and `int2bv` reduces **modulo 2^64**. So a length constraint such as `int2bv(str.len s) == 0` was *also* satisfiable with `str.len s == 2^64`. When that was the only satisfying assignment — `s == ""` with `s` length-0-constrained needs both `int2bv(len)==0` and `s != ""` for a counterexample — the solver picked `len = 2^64`, producing a model with a string longer than its string-model length cap, which broke value parsing (the bogus error). Sound throughout (false-alarm path, never a false proof). **Fix:** when a native `String` symbol is declared, assert `(str.len s) < 2^63` in the Int domain, keeping the `int2bv` conversion faithful (top bit clear, signed value == true length) for *every* native string at once. Gated on `ID_smt_string`, so refined and C/C++ are untouched; full Python sweep byte-identical to baseline. Regression test `string-smt-native-empty-eq`. |
| **native case-transform + `len()` perf** | native | **FIXED 2026-06-22** (`118ccd1739`). Symptom: `upper`/`lower`/`casefold`/`swapcase`/`capitalize`/`title` build a `PYTHON_MAX_STRING_LENGTH`-deep `str.++` concat, so a `len()` over the result timed out (the solver had to reconstruct the concat length position by position) on both cvc5 and z3 for an unconstrained symbolic string. **Fix:** alias the concat to a fresh result symbol `r` and additionally assume `str.len(r) == str.len(obj)` — implied by the concat, so it adds no models; it is a hint that lets `len()` queries discharge by congruence on `len(r)` without folding the concat, while *content* queries still expand `r == concat(...)` exactly as before. This relies on the companion `str.len < 2^63` bound above to keep the hint sound. `len(s.upper())==len(s)` on bounded/unbounded nondet strings now verifies in <1 s. (Nondet *content* equality like `s.upper()==s` remains a separate pre-existing string-solver slowness — verified unchanged by this commit.) Gated on `ID_smt_string`; refined untouched. Regression test `string-smt-native-case-len`. **Whole-group note:** both residuals shared one architectural root — the unfaithful `int2bv`/`str.len` length encoding — so a single length-domain fix unblocked the empty-string equality directly *and* made the case-transform length hint sound. |
| **native produced-string `len()` (concat / `+=` / f-string / `*` / `replace`)** | native | **FIXED 2026-06-22** (`280b6073f2`). A systematic native re-probe found the len-family generalised: an *exact* `len()` relation over any *produced* string timed out on unconstrained symbolic input (`len(s + t) == len(s) + len(t)`, `+=`, f-strings, `s * 3`, size-changing `replace`). A direct SMT experiment isolated the architectural root — the int2bv-wrapped length equality `int2bv(str.len(a++b)) == bvadd(int2bv(str.len a), int2bv(str.len b))` is a cvc5 *timeout*, while the pure-Int form is instant. **Fix:** a reusable `bind_string_length_hint` aliases each produced string to a fresh symbol `r` (`r == produced`) carrying `len(r) == <exact hint>` (concat: `len(a)+len(b)`; repeat: `len(s)*n`; f-string: sum of parts), so len() discharges by congruence; `replace` (constant old/new) uses a sound *directional* bound (`==`/`>=`/`<=` by `len(new)` vs `len(old)`) since its exact length is count-dependent. Sound (hints implied by the alias under the `str.len < 2^63` bound; verified all false length claims still FAIL). Regression test `string-smt-native-produced-len`. |
| regex `re.sub` over a *symbolic* subject — `len()` | native | **residual (hard).** `re.sub` is wired to `str.replace_re_all` (precise for a fixed-length pattern + literal repl), but a `len()` over the result times out: a length hint does not help because the `r == str.replace_re_all(...)` content link drags the (genuinely hard) regex-replace reasoning into the model regardless. Distinct from the concat len-family (cheap producer) — the bottleneck is the regex replace, not the int2bv length encoding. Sound (timeout, never a false proof). |
| regex `IGNORECASE` / literal-symbolic patterns | native | **residual (precision).** `re.IGNORECASE` still degrades to nondet (sound) — a correct fix needs a case-folding pattern *rewrite* (per-literal/per-range, parsing-aware) before lowering; a naive lowercase is unsound (`[A-Z]`→`[a-z]` changes meaning). Literal-symbolic patterns (`re.compile("^"+prefix+"...")`) need front-end pattern *segment-tracking* (the bulk of that work) to splice `str.to_re` holes. Both are precision-only; a-prime `Match`/`None` is **verified sound on native** (`re.match([0-9]+, nondet) is not None` correctly FAILS — no always-`Match` false proof). |
| ↳ IGNORECASE/DOTALL **FIXED** (2026-06-22, `ad89a2ca64`) | both | The row above's "case-folding rewrite before lowering" fix shape is **wrong**. The translator AND the constant matcher already honour a leading `(?i)`/`(?s)`/`(?is)` group (`re_char` case-fold, `strip_inline_flags`) and the `re` stub maps `flags=` to that prefix. Verified: `re.match("(?i)[a-z]+","ABC")` SUCCEEDs, `re.match("[a-z]+","ABC",re.IGNORECASE)` FAILs. The real blocker is that the **constant `flags` argument does not constant-propagate into the stub body** (the stub's `flags` param stays symbolic, so neither `_re_flag_prefix(flags)`'s return nor an inline branch folds — same gap as a minimal `mkprefix(2)` helper, which also fails to fold). General root: "constant args not propagated into a function body." **Fix (LANDED):** a single uniform hook at the top of `convert_call` — for a compile-time-constant `flags` + a string-literal pattern, prepend the inline-flag prefix to the pattern JSON node and zero/drop `flags` before re-dispatch (the stub then sees a flag-free constant pattern, which already works), across all re entry points (match/search/fullmatch/findall/finditer/sub/subn/compile), positional or keyword. Sound: only IGNORECASE/DOTALL (and their union) map to a prefix; any other flag/non-constant-flag/non-literal-pattern is left unchanged → nondet. Verified IGNORECASE does not make a digit class match letters; 0 sweep regressions. **Still open:** literal-symbolic patterns (`re.compile(prefix + "...")`) need front-end pattern *segment-tracking* to splice `str.to_re` holes. Not a translator change. |
| ↳ literal-symbolic: re.escape FALSE PROOF **FIXED** (2026-06-23, `ed4475d00f`) | both | While scoping the literal-symbolic item, found that the documented "symbolic hole → `str.to_re(hole)`" design is **UNSOUND** for general holes: `str.to_re` matches the hole as a literal, but Python interprets it as a regex (`re.match("a+xyz","a+xyz")` is `None` in CPython — `a+` is one-or-more, not the literal `a+`; cross-checked). So a symbolic hole that may contain metacharacters cannot be spliced as `str.to_re` without risking a false proof. Only `re.escape(x)`-produced holes are provably literal. **Related real false proof found + fixed:** the `re.escape` library stub returned `""` (empty regex ⇒ matches everything), so `re.match(re.escape("abc"),"zzz")` wrongly verified. Now modelled soundly in `convert_call`: constant `x` → properly-escaped literal (precise; metacharacters become literal — `re.escape("a.c")` matches `"a.c"` not `"axc"`); symbolic `x` → sound nondet string. The general-hole literal-symbolic precision feature is therefore **left as sound nondet (not implemented)** — implementing it as documented would introduce false proofs. Regression test `re-escape-sound`. |
| ↳ re-stub concrete-default FALSE PROOFS **FIXED** (2026-06-23, `ed4475d00f`/`<expand-groups-subn>`/`<groupdict>`) | both | Whole-group sweep after `re.escape`: the `re` library stub returned **fixed concrete values** instead of sound nondet, which false-proves equality assertions (all cross-checked vs CPython). Fixed: `Match.expand` (`""`→`nondet_str()`), `Match.groups` (`()`→`nondet_list(8, nondet_str())`), `Pattern.subn`/module `subn` count (`0`→`nondet_int()`), `Match.groupdict` (`{}`→`nondet_dict(8)` **plus a `-> dict` return annotation**). Guard tests `re-{expand,groups,subn,groupdict}-result-*-fail` (expect VERIFICATION FAILED). **Architectural root surfaced:** `groupdict == {}` needed the return annotation because a **function-returned dict is not dict-typed at the call site** without one, so `== {}` skips the dict-equality path and defaults true (a *function-return type/value propagation* gap — `def f()->dict` fixes it; lists don't need it). Same root-family as the IGNORECASE constant-flags-not-propagated-into-stub gap. The general function-return propagation fix (so un-annotated user functions don't hit this) remains the broader item; the stub is annotated to be sound now. 0 sweep regressions. |
| ↳ function-return container TYPE inference **FIXED** (2026-06-23, `8ed77f732d`) | n/a (general frontend) | The broader root above is now closed for the **type** dimension: `infer_return_type_from_body` recognised only bare `{...}`/`[...]` literals, so a function returning a dict/list via a CALL (`dict()`/`nondet_dict()`/…), a **comprehension**, or a **local var** bound to one fell through to the int default → type-punned call result → false proofs (`def f(): return dict(a=1); f()=={}` wrongly verified; `return [..]` var likewise). Now driven by `direct_kind`/`rv_container_kind` (the latter resolves a returned `Name` to its one-hop in-body assignment). Soundness: only provably-container returns commit; element-type defaults affect value-read precision, not structural soundness; mixed-shape returns still fall to `python_value`. ESBMC sweep byte-identical to baseline (0 regressions); 642/642 local tests. Guard tests `func-return-{dict-call,dict-var,list-var}-fail`. The `groupdict -> dict` annotation is now redundant with this but kept as explicit documentation. **Value dimension — investigated 2026-06-23, NOT a soundness gap.** Value propagation through function returns **works at runtime** via symex: `def f(): return "abc"; f()=="abc"` verifies and `=="xyz"` fails; `def g(x): return "abc" if x else "def"; g(1)=="abc"` verifies; even `pat = mk(2) + "[a-z]+"; pat == "(?i)[a-z]+"` (call+concat) verifies. The only residual is **conversion-time** constant-folding of a *function call* (the frontend does not partial-evaluate user calls at conversion), which matters solely for conversion-time consumers — in practice the **regex pattern extraction** (regex→SMT translation needs the pattern as a compile-time constant). So a regex pattern derived from a helper call (`re.match(mk(2) + "[a-z]+", s)`) degrades to **sound nondet** (verified precision-only: both `is None` and `is not None` correctly FAIL — no false proof). This is corpus-invisible and the one real case (IGNORECASE) is already handled by the call-site rewrite, so a conversion-time partial evaluator is **not pursued** (heavy + risky for a sound, corpus-invisible precision gain). Recorded as a sound precision residual, not a soundness item. |

**Cross-cutting conclusion:** one-off refined-string axioms hit
diminishing returns; the native backend is the comprehensive answer for
every residual, and is the recommended route for new string precision
work. Keep refined sound + at parity for the default sweep; do not
downgrade refined to gain native. **Caveat (2026-06-22):** the native
backend is corpus-complete but **not** residual-free — the three native
rows above (empty-string-`==` SMT2 error, case-transform+`len()` perf
cliff) and the now-fixed `nondet_string` length divergence were found by
direct probing; native is sound throughout (errors/timeouts, never false
proofs).

### Perf: the default-backend string-refinement cliff vs native (2026-06-24)
Profiling the sweep's 4 TIMEOUTs (`github_3683/3684`, `nondet_list6`,
`redundancy`) established that the dominant perf cliff is the **default
(refined) backend's BV/string-refinement loop**, NOT the shared bounded-dict
struct:
- `github_3683` (`dict[str,dict[str,dict[str,str]]]` iterated with `v["x"] ==
  "y"`): **default times out** (139s at `--unwind 5`, timeout at `--unwind 10`,
  solver itself only ~0.3s — the cost is *many* MiniSAT refinement iterations);
  **native (`--cvc5 --python-smt-strings`) does it in 9s, SUCCESSFUL.** The
  16ᴺ dict struct is identical on both backends, so the explosion is the
  refinement loop over the nested strings, which CVC5's string theory
  dispatches directly. `redundancy` likewise: default 66s vs native <1s (both
  SUCCESSFUL).
- **Implication:** for string-heavy / nested-container workloads the native
  backend is the perf answer (and feeds the **P4 Java** SMT-string story).
  Improving refined-backend string-refinement perf is the harder, lower-ROI
  path. The remaining lever on the *default* backend (smaller nested-level
  container bounds) is a precision/soundness-margin tradeoff; deferred.
- **BUT native is not yet a safe drop-in — two native gaps found:**
  - `github_3684` **ABORTS on native** (rc=134): CBMC-core invariant
    `unpack_struct` — "members of non-constant width should come last in a
    struct". **Root (investigated 2026-06-24):** on native, `python_value`'s
    inline `__str` is the variable-width `smt_string`, so `python_value` itself
    is variable-width. An UNTYPED dict (`d: dict`) has python_value keys AND
    values, so the dict struct `{length, keys[python_value], values[python_value]}`
    has TWO variable-width array members; CBMC's byte-operator lowering can put
    only one non-constant-width member last, so byte-unpacking it (triggered by
    iterating an untyped NESTED dict, `for k,v in d["p"].items()`) aborts.
    Confirmed minimal: untyped nested-dict iteration crashes for BOTH int and
    string keys; a TYPED `dict[str, dict[str, int]]` (concrete, fixed-width
    value structs) does NOT crash. Reordering `__str` last in python_value does
    NOT fix it (the python_value arrays are still variable-width). **Proper
    fix:** make `python_value` constant-width on native — store `__str` as a
    POINTER on the native backend (like `__list_ptr`/`__class_ptr`) instead of
    inline `smt_string`. That reverses the inline-string perf choice (made for
    the refined backend) for native only; a deeper, backend-conditional
    representation change to weigh — NOT a quick reorder. Deferred to a focused
    effort.
  - `nondet_list6` — **NOT a native bug (measurement artifact).** The earlier
    "FAILED on native" was run at `--unwind 5`; the test needs `--unwind 9`
    (6-element list). At its own `--unwind 9` native verifies **SUCCESSFUL**
    (element stability holds on both backends). No divergence.
  **RESOLVED 2026-06-24 ("option A" string boxing).** On the native backend a
  non-fixed-width value that would sit inside a byte-imaged aggregate is now
  boxed behind a typed pointer, keeping the byte-imaged skeleton all
  fixed-width (so byte_extract stays valid) while the smt_string is read via a
  clean typed dereference. Two commits: (1) `python_value.__str` -> `string*`
  (values dimension); (2) dict string KEYS -> `string*[16]`
  (`python_dict_type` / `python_dict_key_elem_type`, with
  `python_dict_logical_key_type` + `python_dict_unbox_key` + boxing in
  `coerce_element`/`box_string_for_storage`). All transforms are type-driven
  no-ops on the refined backend (provably byte-identical; full local suite 0
  regressions). The `github_3684`-class crash is gone and common native
  string-keyed dict ops (construct, subscript read/assign/update, get, pop,
  items, keys, for-in, ==, membership) verify; regression test
  `dict-native-string-key-box`. `github_3684` itself now computes the correct
  value (its assertion SUCCEEDS) but still reports a separate uncaught KeyError
  from the pre-existing symbolic-key-presence modelling gap (a dict-precision
  item, not a string/boxing issue). **Integer case (2026-06-24):** under
  `--python-unbounded-ints` a Python int is the non-fixed-width `integer_typet`.
  `python_value.__int_val` (a 64-bit bitvector) silently truncated wrapped
  values mod 2**64 (unsound) and drove a `simplify_expr` abort on nested int
  containers. `__int_val` is now boxed behind a fresh per-instance `integer*`
  (`allocate_boxed_leaf`), keeping **full precision** with no aliasing. The
  aliasing that initially appeared was NOT the allocation (objects are distinct
  per execution) but the **dict-literal const-fold** re-reading the boxed
  pointer symbol: a dict returned from a function / built in a loop is tracked
  with `__int_val = cast(__box_ptr_N, int*)`, the subscript const-fold returned
  that tracked expression, and symex renamed `__box_ptr_N` to the *latest* value
  (a later instance) → a deterministic false proof. Fix:
  `contains_boxed_leaf_pointer` makes the const-fold skip any value embedding a
  boxed-leaf pointer, falling through to the sound per-instance symbolic read
  (a general correctness fix — re-reading a tracked dict-literal value that
  embeds a per-execution symbol is unsound for any leaf type). The closure
  fn-index stays a precise box (constant → aliasing-harmless). Both `str` and
  `int` boxing are now sound AND precise; default/int64 backend byte-identical
  (0 regressions). Tests `int-unbounded-box-sound` (per-instance precision),
  `int-unbounded-box-no-truncation` (full precision, typed + wrapped),
  `leaf-box-no-alias-string`.

## Linked design records (deep-dives)

- [SMT-LIB String backend design + full implementation ledger](architectural/python-string-phase2-backend-abstraction.md)
  — the intrinsic↔term table, the hybrid→native arc, the per-step
  outcome ledger, and the Plan A completion record.
- [Regex story](python-frontend-regex-story.md) — current regex support
  (shallow stub + `__cbmc_re_*` SMT intrinsics), backend-portability
  matrix, what doesn't work.
- [Regex position plan](python-frontend-regex-position-plan.md) —
  match-position intrinsics design.
- Performance contract between the slicer and string-refinement:
  [perf analysis](architectural/python-perf-analysis.md).

---

## 3. Strings: native SMT-LIB String backend  {#strings}

**Status: NATIVE SMT-STRING BACKEND COMPLETE (Plan A, 2026-06-12); refined
is the default.** `--python-smt-strings` (with `--cvc5`/`--z3`) selects a
*native* SMT-LIB `String` representation end-to-end (the byte-array+`str`
hybrid was retired); the full SMT-theory string surface is precise and fast
there — `==`/`!=`, ordering (`<`/`<=`/…, the refined ceiling), `len`,
`in`/`not in`, `startswith`/`endswith`, `find`/`index`, `+`/concat,
subscript, slice, `replace`, `strip` family (via SMT-LIB regex), f-strings
(incl. `str(int)`/`chr`/`ord`), with model extraction. The
`regression/python` corpus is 540/540 with a verdict under native; refined
remains the no-external-solver default. **Remaining string gaps are narrow:**
on the *refined default* — ordering, substring `replace`, `split` stay
sound-but-imprecise (existential-witness instantiation / list-valued axioms),
all of which the native backend already answers via opt-in; on *native* —
`split` (list-valued) and `casefold`/`title` (Unicode case-mapping, no SMT
primitive) stay sound-nondet. Full detail:
[python-string-phase2-backend-abstraction.md](architectural/python-string-phase2-backend-abstraction.md).
The historical narrative below predates Plan A's completion and is kept as
the design record.

**Status (historical, pre-2026-06-12): PARTIAL.** Python `str` is modelled as
CBMC's refined-string struct (`python_string`, tag
`__CPROVER_refined_string_type`), routed
through the refinement-string solver via `emit_string_function`. The
backend-selector infrastructure has landed: a `python_string_kindt`-style
selector and the `--python-smt-strings` flag exist and are threaded through
`python_language.cpp`. What is **not** done is the migration that would let
the frontend target either backend uniformly, and the SMT-String backend
implementation itself.

**Refined-string precision pass (2026-06-11, landed).** A "sound + precise,
across the board" pass over the refined-string backend landed its
cleanly-achievable wins and characterised the rest by measurement. Landed +
validated (ESBMC sweep: 0 regressions, PASS 2916→2935): symbolic
`rfind`/`rindex` → `last_index_of` (`ee0b89d939`); a dedicated
Python-whitespace `strip`/`lstrip`/`rstrip` axiom (`6cabf82209`, *not* a
`trim` reuse, which is unsound for control bytes). Measured/root-caused as
blocked (kept sound): ordering via `compare_to` (axiom a3's existential
first-diff-index witness isn't instantiated by the refinement); substring
`replace` (existing axiom is char-only); `split` (list-valued); membership
convergence (already handled at HEAD — eager instantiation measured as a net
negative and reverted). **Net: the refined-string precision frontier is
largely tapped; the remaining gaps need either existential-witness
instantiation or new nonlinear/list-valued axioms, for which the SMT-String
backend is the comprehensive answer.** Full per-step ledger:
[python-string-phase2-backend-abstraction.md § consolidated outcome ledger](architectural/python-string-phase2-backend-abstraction.md#string-correctness-plan--consolidated-outcome-ledger-2026-06-11).


**Spike (2026-06-09/10, `github_3090_4`) — diagnosis corrected.** The
blocker is *not* `char*`-vs-`char[]` (JBMC's refined string is *also*
`{length, char*}` and proves fine) and *not* the `array_pool` mechanism
itself. It is **variable indirection + content storage**: `array_pool.find`
already extracts the real array (crash-free, even with symbolic elements)
when the content pointer is the syntactic form `address_of(index(<array>,
0))`, but falls through to a *fresh unconstrained* array when the pointer is
a `member` (e.g. `s.data` once the string is stored in a variable). Adding
an explicit association fixed `chr(i)=="f"` + `github_3090_4/5` under the
default backend (soundly) **but crashed `github_3130_fail`** — because
`chr` used *static, shared* content storage, so one constant pointer was
re-associated across loop unwinds. JBMC avoids this by giving each string
**per-execution heap content** (distinct pointer per iteration), so the
real bottleneck is **per-execution content storage**, not association.
**Two viable, sound routes:** (a) JBMC-style per-execution storage (fresh
allocation per producer) + the existing association — the "bigger change";
(b) **symex content-pointer dereference** — verified feasible
(`value_set_dereferencet` resolves `*(p+i)` for symbolic `i`; hook is the
already-`cprover_string`-scoped `constant_propagate_assignment_with_side_
effects`), re-materialising a bounded literal array that routes through
`find`'s crash-free fast path with **no front-end storage change** and **no
loop crash**, at the cost of bounded-deref perf + a scoped core-symex
change. Route (b) is the lower-impact lean. A throwaway **prototype
(2026-06-10, reverted) validated route (b)**: `chr(i)=="f"` and
`chr(122) not in "abc"` prove **soundly** and `github_3130_fail` is
**loop-safe (no crash)** — but a *broad* `symex_assign` hook regressed 9
sweep tests (constant strings, multibyte UTF-8 `chr`, concat results) with
0 new gains, so a production version must be **carefully scoped** (only the
symbolic/variable-indirection case; preserve constant/literal/multibyte
fast paths; materialise concat *producers*, not just comparison operands).
**Decision (2026-06-10): route (b) is adopted** as the production direction
— symex resolves content pointers to their array *object* (not per-element
unroll) for static-value leaves, while produced/heap-backed content uses
association (a Java migration was assessed and found **not applicable** —
Java's strings are heap-backed and genuinely need association; the
front-ends converged on association for produced content). **Phase 1 landed
(2026-06-10):** symbolic `chr`
content equality/contains and symbolic-`chr` concat chains
(`github_3090_4/_5`) prove soundly and loop-safely, zero sweep regressions;
see the design-decision section. **Phase 2 landed (2026-06-10):**
refinement-produced results (concat, substring, `str(int)`, ...) get fresh
per-execution real backing installed in the symex const-prop handlers
(Python-gated; JBMC `jbmc-strings`/`strings-smoke-tests` green), so
byte-level/chained ops on them — `(chr(i)+"oo")[0]`, iteration of a concat
result — prove and are loop-safe; zero sweep regressions. Implementation
discipline + sequencing in the
[design-decision section](architectural/python-string-phase2-backend-abstraction.md#design-decision-2026-06-10-choice-b--symex-content-pointer-resolution).
The SMT-string backend (`--python-smt-strings` / CVC5) remains an orthogonal
precision option.
Full analysis (JBMC loop handling, `find` fast path, storage options,
symex-deref pros/cons, prototype results):
[python-string-phase2-backend-abstraction.md](architectural/python-string-phase2-backend-abstraction.md#update-2026-06-10--corrected-conclusion--symex-deref-feasibility).

**Plan (5 phases; phases 1–2 designed, 3–5 open):**

> **Superseded (2026-06-11).** The single current plan for all deferred string
> work — the `smt_string_typet` refactor (Plan A), interim SMT model extraction
> (Plan B), and the refined-backend axioms replace/repeat/strip(chars)/split/
> casefold/count and the compare_to existential (Plan C1–C6) — lives in
> [python-string-phase2-backend-abstraction.md § Consolidated forward plan](architectural/python-string-phase2-backend-abstraction.md#consolidated-forward-plan-2026-06-11--supersedes-earlier-scattered-plans).
> The 5 phases below are retained for the site inventory (phase 1) only.


1. *Inventory* (done) — ~50 frontend sites reach into the refined-string
   struct (`build_string_struct`, `.length`, `.data[i]`, scratch-loop
   comparisons), classified as producers / consumers / mutators.
2. *Backend abstraction design* (done) — opaque string handle, a literal
   constructor intrinsic, and a per-intrinsic lowering table. The detailed
   spec (intrinsic ↔ SMT-LIB term table, `smt_string_typet`, backend impact
   on `smt2_conv` / `boolbv` / `string_refinement`, PR ordering) lives in
   [architectural/python-string-phase2-backend-abstraction.md](architectural/python-string-phase2-backend-abstraction.md)
   — keep that as the implementation spec.
3. *Frontend refactor* (open) — migrate every site off the concrete struct
   onto intrinsics: `build_string_struct` → `cprover_string_literal_func`
   (33 callers), `.length` → `cprover_string_length_func` (17),
   `.data[i]` → `cprover_string_char_at_func` (7), concat/repeat producers
   → `cprover_string_concat_func` / a new `cprover_string_repeat_func`, and
   add the 9 not-yet-available intrinsics (substring/replace/split/strip/
   find-from) with `ID_` entries + axiom handlers. One PR per group.
4. *Backend implementations* (open) — implement each intrinsic's SMT-String
   lowering in `smt2_conv.cpp` per the phase-2 table; add `smt_string_typet`
   and its `convert_type` mapping to SMT `String`.
5. *Retire hacks* (open) — remove the `smt2_conv` subject-literal
   materialisation workaround and the Python→C `str` marshalling special
   case once 3–4 land.

**Why it matters:** this unblocks precise symbolic regex (see
[§4](#regex)) and removes a class of refined-string ↔ pointer-analysis
performance cliffs (related to [§5](python-frontend-plan.md#dict-byref)).

---

## 4. Regex (`re` module)  {#regex}

**Status: PARTIAL.** Current support is a shallow library stub plus
`__cbmc_re_*` SMT intrinsics, and a Stage-1 call-site `regex-no-match`
check that flags statically-impossible matches. The current-state
reference is [python-frontend-regex-story.md](python-frontend-regex-story.md).
The architectural invariant: the **frontend emits refined-string
arguments; the backend bridges them to SMT `String`**.

**Already built (verified 2026-06-08):**

- The **subject → SMT-String bridge (Approach C2) is implemented** in
  `smt2_conv.cpp` (regex-intrinsic interception around the
  `cprover_string_{match,search,fullmatch}_func` lowering): a constant
  pattern is translated by `python_regex_to_smt.cpp`, a constant subject
  lowers to a precise `(str.in_re "subj" re)`, and a *symbolic* subject is
  bridged from the refined-string struct via
  `str.++ (str.from_code (bv2nat (select array i)))` truncated to length.
  Unsupported patterns / unrecognised subject shapes fall back to a sound
  `bv0`.

**The actual remaining gaps (the Wave-2 payoff), verified 2026-06-08:**

1. **Symbol subjects fall through to `bv0` (the deep gap).** The bridge's
   subject extractor only recognises a *syntactic* refined-string
   `struct_exprt{len, address_of(index(array, 0))}`. A subject that is a
   plain symbol (the common `s = nondet_str()` case) — whose bytes live in
   the string-refinement `array_pool`, not syntactically in the expr —
   hits the sound `bv0` fall-through, so the match is *never* taken and
   queries over symbolic subjects are vacuous (measured: both
   "`matches ⇒ len≥1`" and the contradictory "`matches ⇒ len==0`" verify
   SUCCESSFUL, i.e. the branch is unreachable). Closing this needs
   `smt2_conv` to expose an `array_pool`-tracked refined string to the
   SMT-LIB String theory — deep CBMC-core work (shared with JBMC code
   paths; the spec mandates a JBMC regression run per PR). Constant-subject
   matching already works precisely under `--cvc5` (`re-wave2-cvc5`).
2. **Library `Match`/`None` result not tied to the intrinsic — RESOLVED
   (2026-06-17, `aa71a43868`); no flag.** The `re` stub now branches on the
   intrinsic (`if __cbmc_re_match(p,s): return Match() else: return None`), and
   the **default (refined-string) backend decides a CONSTANT pattern + CONSTANT
   subject precisely** so the branch is exact (matched → `Match()`, proven
   no-match → `None`) without `--cvc5`. This needed neither a flag nor the deep
   array_pool work feared here: a conversion-time backtracking matcher
   (`python_regex_match`, supported subset; conservative `std::nullopt` →
   sound nondet) is invoked at SOLVE time inside the refined solver's
   `match/search/fullmatch_func` handler (the re stub body is converted once
   with symbolic params, so the literals are only available post-symex, where
   `get_string_expr(array_pool,·).content()` yields the constant bytes). A
   SYMBOLIC subject on the default backend stays a sound nondet Match-or-None
   (precise on native via `str.in_re`). Sweep PASS 2930→2945, 0 regressions, 12
   regex tests (`re1/3/4/5/6/8/9/10/11/12`, `github_3013/_2`) DIFF → PASS.
   (The old `--python-strict-re-result` flag idea is dropped — the default is
   now both sound and precise for the decidable case.)

**Update (2026-06-12) — native SMT-String backend.** With `--python-smt-strings`
now selecting the native `smt_string` representation (the byte-array hybrid is
retired), the **subject** side of gap 1 is resolved: a symbolic subject is
already an SMT `String`, so `(str.in_re <symbolic-subject> <RegLan>)` is precise
with no `array_pool` extraction. One prerequisite fix: the
match/search/fullmatch lowering's `extract_literal()` recognises only the
refined `{length, address_of(array)}` struct, so under native — where the
pattern is an `smt_string` *constant* — it returns `nullopt` and degrades to
nondet. Teaching it to read the pattern from an `smt_string` constant restores
regex precision under native (the **native regex pattern-extraction fix**;
small, prerequisite for everything below).

> **⚠ SOUNDNESS WARNING (2026-06-23).** The design below is **unsound as
> written** and must NOT be implemented for general symbolic holes. `str.to_re`
> matches a hole as a *literal*, but Python interprets a pattern substring as a
> *regex*: `re.match("a+xyz","a+xyz")` is `None` in CPython (`a+` is one-or-more
> `a`, not the literal `a+`), whereas `str.to_re("a+xyz")` would match it →
> false proof. A symbolic hole can carry ANY regex syntax, so splicing it as
> `str.to_re` is unsound in both directions. The ONLY provably-literal hole is
> one produced by `re.escape(x)` — which is now modelled soundly (constant →
> escaped literal; symbolic → nondet; see the gaps table). Until holes are
> tracked as escape-derived, the general literal-symbolic feature stays sound
> nondet. The text below is retained only as the (corrected-scope) design
> record for an escape-tracked implementation.

**Extension — structurally-constant patterns with symbolic literal substrings
(native).** The pattern must remain *structurally* constant (its regex
operators known at conversion time): SMT-LIB `RegLan` is built only from regex
constructors (`re.union`, `re.*`, `re.range`, `str.to_re` of literals) and has
no operation that interprets a *symbolic* string as a regex — `str.to_re(p)`
accepts exactly the literal `p` (i.e. equality, not pattern semantics), so a
fully-symbolic pattern degrades soundly to nondet. **But** a pattern whose
*structure* is a compile-time constant while its *literal substrings* are
symbolic — e.g. `re.compile("^" + prefix + "[0-9]+$")` with `prefix` a runtime
`str` — is expressible as
`(re.++ (str.to_re prefix) (re.+ (re.range "0" "9")))`: `str.to_re` on the
symbolic literal "holes", `re.*` constructors for the constant structure.

  *Design.*
  - **Front-end:** when an f-string / `+`-concatenation forms a regex pattern,
    carry it not as one flattened literal on the intrinsic but as a **list of
    segments**, each either a constant pattern fragment or a symbolic
    `smt_string` literal-hole. (Today the pattern is flattened to a single
    literal, which loses this structure; a new intrinsic variant would take the
    segment list.)
  - **Backend** (`python_regex_to_smt.cpp` + the smt2_conv lowering): translate
    constant fragments as today and emit `(str.to_re <hole>)` for each symbolic
    hole, splicing them into the `RegLan` term with `re.++`.
  - **Soundness / scope:** a symbolic hole is matched **literally** (spliced
    verbatim as a `str`), which is exactly the intended semantics for the
    `re.compile("..." + x + "...")` / `re.escape(x)` idiom. A hole meant to
    carry regex *metacharacters* is out of scope (that is a fully-symbolic
    pattern → nondet). Constant-only and fully-symbolic patterns are unchanged.
  - **Effort / ordering:** front-end segment-tracking is the bulk; the backend
    splice is small. Builds on the native pattern-extraction fix and the
    `re.*`-wrapper routing ("a-prime") refactor, so it is sequenced after both.

- **Compilation flags** (`re.IGNORECASE` etc.): currently fall back to
  nondet. *Fix shape:* rewrite the regex AST per flag before lowering.

**Implementation findings (2026-06-12).**

- **Native regex pattern-extraction fix — LANDED** (commit `5231f3b61d`).
  `__cbmc_re_{match,search,fullmatch}` is now precise under
  `--cvc5 --python-smt-strings` for a constant pattern over **both constant and
  symbolic subjects** (incl. character classes), via `str.in_re` directly on
  the `smt_string` subject. This closes the old "symbol subjects fall through to
  `bv0`" gap (gap 1) for the native backend — the refined bridge is no longer on
  the path. (`string-smt-native-regex`.) A CVC5 perf edge remains: combining
  `str.in_re` with a `len()` query on the same symbolic subject can time out;
  match/no-match decisions themselves are fast.
- **a-prime is *result-precision*, not *routing*.** The `re.*` stub **already
  calls** `__cbmc_re_*` (for the SMT side-effect). The remaining work is to make
  the returned `Match`/`None` *reflect* the intrinsic result. This is entangled:
  the current always-`Match()` is itself a latent **unsoundness** (it never
  explores the `None` path, so a missing-`None`-guard bug such as
  `re.match(...).group()` on a non-match is not caught), but switching to real
  `Match`/`None` makes the result **nondet under the default backend** (the
  intrinsic is nondet there), which changes many benchmark outcomes. So a-prime
  needs an opt-in `--python-strict-re-result` flag (off by default) and a
  stub→flag mechanism, not just a stub rewrite. Higher-stakes than the doc
  implied; prerequisite for the literal-symbolic and `re.sub` items having
  real-world reach.
- **Literal-symbolic patterns: anchor-soundness caveat.** The fragment-wise
  composition (above) must handle `^`/`$` only at the *whole-pattern*
  boundaries; a `^` at the start of a non-first fragment or `$` at the end of a
  non-last fragment must **bail to nondet** (not be stripped per-fragment),
  otherwise the regex is over-permissive (unsound). The translator currently
  strips leading-`^`/trailing-`$` per input string, so a body-only fragment
  translator + boundary handling is required.
- **`re.sub` native:** expressible via CVC5 `str.replace_re_all` (a new
  `__cbmc_re_sub` intrinsic + `str.replace_re_all` lowering), but also entangled
  with the stub (`sub` returns `""` today) and only reaches real code via
  a-prime-style routing.

---

## Native robustness: smt_string members in byte-operated structs  {#native-byte-ops}

**Status: PARTLY RESOLVED — the dict-by-reference-mutation crash is fixed; a
general byte-op gap remains for other shapes (native only; sound — crashes,
never a false proof).**

The original trigger — **dict pass-by-reference *mutation*** through an
`Any`/`python_value` parameter (`def f(d): d["k"]=v` then asserting the caller
sees the mutation) — **no longer crashes** and now propagates *precisely* under
native (see [the §0 by-reference fix](python-frontend-plan.md#false-proofs)): the argument is promoted
to a clean, field-sensitive `dict[str, value]` temp instead of being reached
via a byte-reinterpreting opaque `__class_ptr` cast, so no `byte_extract` /
`byte_update` is generated for it.

The underlying lowering limitation is still present for *other* shapes: a struct
that embeds an `smt_string` member (e.g. `python_value.__str`, or a class/dict
struct holding a string) cannot be **byte-operated**
(`byte_extract`/`byte_update` → `lower_byte_operators`), because `smt_string`
has no fixed bit-width and the lowering requires non-constant-width members to
come last: `lower_byte_operators.cpp` fires *"members of non-constant width
should come last in a struct"*. The `regression/python` corpus (543/543 under
native, 0 crashes) does not currently exercise a remaining instance, so it is
latent. Candidate fixes (both shared-code, non-trivial):
(a) teach `lower_byte_operators` to treat `smt_string` members opaquely (NB: a
naive "replace the unlowerable byte op with a fresh nondet" is **unsound** when
the byte op is a write whose effect must alias a caller object — it silently
drops the mutation; only safe for genuinely value-less reads);
(b) order `smt_string` members last in the affected struct layouts. Lower
priority than the regex items; recorded so it is not mistaken for soundness.
- **Wave 3 — native regex axioms in the string-refinement loop: NO PLAN
  YET** (research-grade; deferred). Back-references, lookahead, and capture
  groups are explicitly out of scope.

---

## Native backend: class-struct str FIELDS must be boxed (2026-07-19, backtraced)

The remaining 7-corpus-program TOERR family under `--python-smt-strings
--smt2 --cvc5` has ONE representation-level root, established by debug-build
backtrace (`unpack_struct` at lower_byte_operators.cpp:720 via
`smt2_convt::lower_byte_operators` -- SSA conversion, not symex):

- `python_value.__str` is boxed (fixed-width pointer -- the Plan A "string
  boxing" invariant), but CLASS STRUCT str fields (`self.region_name: str`)
  remain INLINE `smt_string` members;
- every generic identity/tag read through the opaque `__class_ptr`
  (`*(int32*)ptr` -- isinstance, the per-instance provenance dispatch
  guards, truthiness) byte-extracts the pointed struct; a variable-width
  member makes `unpack_struct` abort.

Two earlier same-family sites were fixed 2026-07-19 (kwargs-dict KEY packing
`690d05b9a0`, 39->7 TOERRs; the None-for-str refined marker `5c1d9a0f8e`).
The remaining fix is the representation completion: under the native
backend, box str-typed CLASS FIELDS behind `python_boxed_string_ptr_type()`
exactly like `python_value.__str` (touch points: the class struct builder in
convert_class_def, field read/write coercion, ctor init, shadow fields).
A field-boxing pass at the struct-definition locus + boxing-aware
member access mirrors the existing `coerce_element`/`box_string_for_storage`
choke points, so the change is architectural but bounded.

**2026-07-20 verdict: pointer boxing is NOT sufficient (implemented,
measured, REVERTED).** Boxing str fields behind typed pointers (struct
builder rewrite + convert_attribute deref wrapper + coerce_assign_rhs
boxing + static class-default anchoring) took the corpus from 7 to 4
TOERRs -- but broke 4 regex-native CORE tests: when the value set cannot
resolve a boxed-field deref field-precisely, byte lowering byte-extracts
the POINTED smt_string instead (`bv_to_expr` abort at
lower_byte_operators.cpp:403) -- the pointer transform MOVES the
variable-width byte-extraction, it does not eliminate it. The honest
requirement for native corpus-readiness is a FIXED-WIDTH string
representation reachable without byte-imaging on ALL paths. Candidates,
in rough order of promise:
1. a solver-side STRING-ID HANDLE (int32 index into a solver-managed
   string table) as the universal in-aggregate representation -- fully
   fixed-width, no pointers, no derefs; smt2_conv maps handle ops to
   String terms (a Plan-A extension at the backend boundary);
2. core-CBMC: teach lower_byte_operators to treat smt_string as an
   opaque fixed-width token (requires backend agreement on width);
3. suppress byte-granular identity reads (the *(int32*)__class_ptr
   family) in favour of field-precise struct-typed derefs -- helps only
   when value sets resolve, as the revert showed.
**2026-07-20 pm: candidate 1 SPIKED GREEN for class fields
(`894514d30d`).** The string-id handle needed ZERO backend changes:
strtab is an ordinary mathematical-function symbol (find_symbols declares
the UF, the generic function-application path applies it, convert_type
already handles the String codomain). Class str fields are handles; reads
map h -> strtab(h) at the convert_attribute choke point; writes allocate
fresh handles with ASSUME strtab(h) == value in coerce_assign_rhs.
Frontend-LIBRARY classes are exempt (re.Pattern.pattern carries a backend
contract: the smt2 regex lowering recovers the pattern by constant
propagation, which the UF blocks). Native corpus TOERRs 7 -> 4 with the
regex-native suite GREEN (pointer boxing had broken it).

**2026-07-21: the invariant GENERALISES beyond strings (user-observed,
probe-confirmed).** `--python-unbounded-ints` has the SAME latent family:
the boxed `integer*` payload points at a variable-width mathematical
integer, and `apigateway_key_manager --python-unbounded-ints --smt2
--cvc5` aborts in the SAME `unpack_struct` invariant (small probes pass
because their value sets resolve the derefs -- exactly the string
history). The unified requirement is a representation INVARIANT:

> **No variable-width type (smt_string, mathematical integer) may appear
> in any aggregate or behind any pointer that byte-granular accesses can
> reach.** Fixed-width HANDLES + a solver-side denotation table
> (`strtab : bv64 -> String`, and analogously `inttab : bv64 -> Int`)
> are the uniform in-aggregate representation; the UF machinery needs
> zero backend changes (spiked green for str class fields, 894514d30d).

**CLOSE-OUT 2026-07-21 (`81b3c5c7fd`).** Handles vs pointers, settled: a
pointer's denotation goes through the MEMORY MODEL (unresolved derefs
byte-image the pointed variable-width object); a handle's denotation is a
pure UF application over its VALUE -- nothing to resolve, provenance-free,
survives byte copying. Pointers buy aliasing (needed for mutables);
strings/mathematical ints are immutable values -> handles.

Landed: dict KEYS (pointer box -> handles) and dict VALUES
(python_dict_type enforces the invariant; coerce_element write choke
point; rvalue-dispatch + loop-bind read unwraps); inttab for
--python-unbounded-ints (__int_val member + both readers + wrap/defaults;
exact big-int round-trips CORE-pinned -- the integer* box hit the same
unpack_struct family, probe-confirmed).

Deliberately NOT handle-ized (backend INTRINSIC CONTRACTS, each found by
CORE regressions): the pv __str payload (smt2 regex lowering recovers
string CONSTANTS syntactically -- UFs block constant propagation) and
list[str] ELEMENTS (findall/split DELIVER String results into list slots
backend-side). **2026-07-21 pm (`14981b1b15`, `856675967a`): list[str] elements LANDED
as handles** after three diagnosis rounds (recorded at python_list_type):
the "intrinsic delivery" theory was wrong (findall/split build lists in
Python stub code); the real blockers were handle-blind list-element
COMPARISONS (strtab-aware equality branch), a second append emitter
bypassing coerce_element, mixed-representation construction (zero-fill
with the raw element type; the string-method list builders pushing raw
literals), and loop variables typed as the slot instead of the
denotation. The coerce_element CONTEXT RULE (str -> any bv64 element
slot = handle) backs up the fragile comment-flag recognition.

The remaining 4 native corpus TOERRs are now solely the pv __str POINTER
member, locked by the smt2 regex constant-recovery contract (the regex
lowering recovers pattern/subject constants syntactically; both a UF
handle and an unresolvable pointer defeat it). The one remaining,
precisely-scoped native item: strtab-aware constant recovery in
smt2_conv (follow `strtab(h)` applications whose h has a unique
definitional ASSUME) -- a small backend extension, after which __str can
become a handle and the corpus TOERRs should reach zero. First
unbounded-ints corpus baseline: 10 TOERR (entangled with refined-string
issues; experimental config).

**2026-07-22 (`c0d0469211`, `7f3c0adbf3`): DONE -- native corpus
CORPUS-READY at 0 TOERRs** (CLEAN 38 / TP 8 / FP 0 / TIMEOUT 1, full
default-mode parity; the timeout is the known SAT-bound aws_untagged).
strtab-aware recovery landed as scoped (shared
`try_extract_string_literal` + stepwise `try_recover_strtab`; ghost-Bool
ASSIGNMENT axioms because SSA conversion order is assignments-first;
ghost-form-only recording to avoid reverse-edge chase cycles; keying by
identifier/handle-id because the ID_C_ comment flag varies). It
discharged the regex contract, so BOTH deferred migrations completed:
pv __str -> handle AND dict keys pointer-box -> handle (the pointer box
was itself byte-imaged through unresolved value-set derefs), plus the
library-class exemption lift. Guards: string_to_handle rejects
non-smt_string payloads (bytes / bridge structs -> unconstrained image,
sound); coerce_element unwraps pv through the string denotation for
string-shaped slots; parameters re-annotated in the body REBIND instead
of retyping in place (call-contract). Recorded residuals: interproc
dict.pop membership precision under native (sound direction);
unbounded-ints experimental config unchanged.
