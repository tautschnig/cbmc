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

