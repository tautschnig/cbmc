# Design: faithful PERFORM / paragraph control-flow lowering

Status: **implemented** (with a refinement — see note below). Author: Kiro.

> **Implementation note (refinement).** The shipped implementation
> realises the return mechanism with **one re-entrant parameterised
> function `$proc(entry, exit)`** rather than the manual perform-return
> stack + resume-dispatch of §3. All paragraphs live in `$proc` as
> labelled regions (so `GO TO`/fall-through stay local branches); an
> entry dispatch jumps to `entry`, a per-paragraph end check returns at
> `exit`, and `PERFORM p THRU q` is the call `$proc(index(p), index(q))`.
> CBMC's own call stack then *is* the perform-return stack, and a
> recursive `PERFORM` is a recursive call bounded by `--unwind` — with no
> manual stack, no resume ids, and no spurious loop back-edges for the
> acyclic case (so the default baseline is preserved except for the one
> genuinely-recursive program). `STOP RUN`/`GOBACK` set a shared
> `$stopped` flag that unwinds out of every frame. This is strictly
> simpler than §3 and is why §4's "literal function-per-paragraph"
> objection does not apply: there is exactly *one* function, entered at a
> parameterised label, not one per paragraph. The §3 design is retained
> below as the rationale and the considered-alternatives record.

This document designs the replacement of the current *inlining* lowering
of `PERFORM` with a control-flow model that represents the COBOL
procedure-division control graph faithfully — including recursive /
re-entrant `PERFORM`, `GO TO` across paragraph boundaries, `THRU`
ranges, and fall-through — and lets CBMC bound recursion with `--unwind`
instead of silently pruning it.

It is grounded in the IBM Enterprise COBOL for z/OS 6.4 Language
Reference (hereafter *LR*), chapter "Procedure division" —
"PERFORM statement", "GO TO statement", "EXIT statement", and
"Explicit and implicit transfers of control".

---

## 1. Problem statement

### 1.1 What the frontend does today

The whole `PROCEDURE DIVISION` of one `PROGRAM-ID` is lowered to **one**
goto-function (`cobol::<program-id>`). Paragraphs are emitted in source
order as `code_labelt` regions; physical fall-through between adjacent
paragraphs is preserved; `GO TO` is a `code_gotot` to the target
paragraph label; `STOP RUN` / `GOBACK` jump to a final
`program_end_label`. (`build_function`, `gen_statement` in
`src/cobol/cobol_typecheck.cpp`.)

`PERFORM` is lowered by **inlining**: `gen_perform_invocation` copies the
statements of the performed paragraph range `[start..end]` into the call
site. The `TIMES` / `UNTIL` / `VARYING` variants wrap that inlined copy
in a counter/condition loop. A paragraph that is (transitively)
performed while it is already being inlined is detected with an
`inlining` set and its re-entrant copy is replaced by `assume(false)`.

### 1.2 Why that is inadequate — three measured failures

Experiments (run against `build/bin/cbmc`, artifacts since removed):

1. **Exponential IR blow-up.** A 12-level chain in which each paragraph
   performs the next *twice* expands to ~2048 copies of the leaf
   paragraph; the resulting `cobol::BLOWUP` goto-function has **12 306
   instructions**. Inlining is exponential in `PERFORM` nesting depth.
   Real programs nest performs (CardDemo: 1304 `PERFORM`s across 44
   programs), so this is a scaling wall, not a corner case.

2. **Recursion is unsound, not just bounded.** For a self-performing
   `COUNT-DOWN` paragraph, `assert WS-ACC = 5` reports SUCCESS — but so
   does `assert WS-ACC = 999`. The `assume(false)` on the re-entrant
   path makes *every* path that reaches a post-recursion statement
   infeasible, so all downstream assertions pass **vacuously**.
   `--unwind 8` does not help, because the recursion was resolved at
   *translation* time; there is no loop left for CBMC to unwind. This is
   a silent soundness hole: a real bug after a recursive `PERFORM` is
   masked.

3. **It cannot express the common error-handler cycle.** CardDemo's
   `COACTUPC`/`COACCT01` have `9000-ERROR → 8000-TERMINATION →
   5200-CLOSE-… → 9000-ERROR`. Inlining such a cycle does not terminate;
   the `assume(false)` cut is what keeps it finite, with the vacuity
   cost above.

### 1.3 The design tension

The reason this is hard — and why it must be reworked "all at once" — is
that COBOL paragraphs have a **dual role**:

- In main-line flow they execute in sequence by **fall-through**.
- As a `PERFORM` target they are entered and must **return** to the
  statement after the activating `PERFORM` when control reaches the end
  of the range (LR, "PERFORM statement": "the return mechanism … is set
  up to … return control to the … statement following the PERFORM").

and `GO TO` is an **unconditional branch that crosses paragraph
boundaries and does not establish a return point** (LR, "GO TO
statement"). Critically, the idiomatic COBOL early-return is
`PERFORM X THRU X-EXIT` with `GO TO X-EXIT` inside the range: in the
corpus there are **235 `*-EXIT` paragraphs** and **119 `GO TO *-EXIT`**
branches. So a faithful model must let a `GO TO` to a range's exit
paragraph trigger that range's `PERFORM` return.

A naive "each paragraph becomes its own goto-function, `PERFORM` becomes
a function call" mapping founders precisely here: `GO TO` would have to
jump from inside one function into another, which the call/return model
forbids.

---

## 2. Design goals

1. **Faithful return semantics**, including `THRU` ranges and the
   `GO TO range-exit` early return.
2. **Recursion is bounded by `--unwind`, never vacuously pruned.** A
   false assertion reachable at bounded recursion depth must FAIL.
3. **No translation-time blow-up**: each paragraph's statements are
   emitted **once**.
4. **`GO TO` and fall-through stay plain branches** (no change to their
   meaning), since they are already correct and pervasive.
5. Reuse the existing `TIMES`/`UNTIL`/`VARYING` loop lowering and the
   inline-`PERFORM` (`PERFORM … END-PERFORM`) path unchanged.

---

## 3. Recommended design — single function + perform-return stack + finite resume dispatch

Keep the **one-goto-function, paragraphs-as-labelled-regions** layout
(so `GO TO` and fall-through remain `code_gotot` / physical sequence,
unchanged). Replace inlining with an explicit **return mechanism** that
mirrors the hardware the LR describes:

### 3.1 State

Introduce three program-scope variables in the function:

- `__cobol_perform_sp` : index into the perform stack (an integer).
- `__cobol_perform_exit[]` : array; `exit[k]` is the **paragraph index**
  whose end should pop frame `k` and return.
- `__cobol_perform_resume[]` : array; `resume[k]` is the **resume id**
  (a small integer naming the statement after the activating `PERFORM`).

The stack has a fixed maximum depth `D` (e.g. 256) with an
`assert(__cobol_perform_sp < D)` on push — overflow is a reported
property, not silent corruption. (A dynamic-array alternative is noted
in §6.)

### 3.2 Out-of-line `PERFORM p THRU q` (the `ONCE` primitive)

Each `PERFORM` *site* is assigned a unique compile-time **resume id** `R`.
The site lowers to:

```
  assert(__cobol_perform_sp < D);                 // stack overflow check
  __cobol_perform_exit[__cobol_perform_sp]   = index(q);
  __cobol_perform_resume[__cobol_perform_sp] = R;
  __cobol_perform_sp++;
  goto para_label(p);
resume_R:                                          // dispatch lands here
  // … statements after the PERFORM continue …
```

`TIMES` / `UNTIL` / `VARYING` wrap this primitive exactly as they wrap
`gen_perform_invocation` today: the loop body is a single perform-range
activation. The lexical inline `PERFORM … END-PERFORM` is unchanged (it
has no paragraph identity and cannot recurse, so it is still emitted in
place).

### 3.3 Paragraph-end return check

At the **end of every paragraph** `i` (the fall-through boundary to
paragraph `i+1`) insert:

```
para_end_i:
  if(__cobol_perform_sp > 0 &&
     __cobol_perform_exit[__cobol_perform_sp - 1] == i)
  {
    __cobol_perform_sp--;
    __cobol_pc = __cobol_perform_resume[__cobol_perform_sp];
    goto dispatch;
  }
  // else: fall through to paragraph i+1 (physically next; no code)
```

So only the **range-exit** paragraph `q` returns; paragraphs in the
middle of a range fall through normally. A `GO TO q-EXIT` therefore
reaches `para_end_{q-EXIT}` and returns — modelling the idiomatic early
return for free, with no special case.

### 3.4 The resume dispatch

Resume targets are *mid-paragraph* (the statement after a `PERFORM`), and
goto targets must be static, so resume ids are dispatched through one
finite `switch`:

```
dispatch:
  switch(__cobol_pc) {
    case R1: goto resume_R1;
    case R2: goto resume_R2;
    …                                  // one case per PERFORM site
  }
```

The number of resume ids equals the number of `PERFORM` sites — finite
and known at compile time — so `dispatch` is a static switch, not a
computed goto.

### 3.5 Why this is correct and bounded

- **Return semantics**: control returns to the activating `PERFORM`
  exactly when it reaches the end of the range-exit paragraph, per LR.
  Nested performs stack; the innermost pending exit is on top.
- **`GO TO` early return**: a branch to a range-exit paragraph hits that
  paragraph's end check and pops — matching the `PERFORM X THRU X-EXIT`
  + `GO TO X-EXIT` idiom.
- **Recursion is a loop now**: a recursive `PERFORM` pushes another
  frame and re-enters the paragraph via a **backward** `goto` (paragraph
  start) and the `dispatch` back-edge. CBMC treats these back-edges as a
  loop and unwinds them; `--unwind N` bounds recursion depth. There is
  **no `assume(false)`**, so a post-recursion assertion reachable within
  the bound is actually checked — fixing failure (2).
- **No blow-up**: each paragraph's statements are emitted once; the cost
  is O(#paragraphs) extra end-checks plus one dispatch switch — fixing
  failure (1).

### 3.6 Interaction with existing constructs

- `STOP RUN` / `GOBACK` / `EXIT PROGRAM`: `goto program_end_label` as
  today (they abandon the perform stack — correct; LR "STOP RUN").
- `EXIT` statement and `*-EXIT` paragraphs: `EXIT` is a no-op; the return
  is produced by the paragraph-end check. No special handling.
- `GO TO … DEPENDING ON`: unchanged (computed `goto` over labels).
- `ALTER`: still refused (obsolete; not in corpus).
- `PERFORM … THRU` where control `GO TO`s *past* the exit paragraph and
  never returns through it: the frame stays on the stack until some
  later paragrap-end matches a pending exit, or `program_end`. This
  matches a single-return-point machine and is the documented modelling
  choice for this (LR-undefined) situation.

---

## 4. Considered alternative — literal function-per-paragraph

Map each paragraph to its own goto-function and `PERFORM p THRU q` to a
driver that calls the functions for `p … q` in order; recursion then
uses CBMC's native call unwinding.

Rejected as the *general* model because **`GO TO` cannot cross
goto-function boundaries**. A `GO TO r` in paragraph `p` that targets a
paragraph in another function cannot be a branch. Salvaging it requires a
trampoline: each paragraph function returns a "next action"
(fall-through / `GO TO x` / performed-range-done) to a top-level
dispatcher loop — which is exactly the dispatcher + state of §3, only
with the per-paragraph code split across functions for no added benefit
(CBMC inlines small functions at symex time anyway, and the split
complicates source-location and trace mapping).

A *restricted* function-per-paragraph model is viable for the structured
subset where every `GO TO` targets the current range's exit (true for
119 of 186 corpus `GO TO`s). It could be a later optimization for
programs that pass a static "well-structured" check, falling back to §3
otherwise. It is **not** the primary design because it does not cover the
full language.

---

## 5. Migration plan (incremental, suite stays green)

The change is to `gen_*`/`build_function` only (the parser and the
`stmtt` tree are unchanged). Staged so the regression suite and the
CardDemo baseline (33/44 default, 44/44 `--unwind 3`) are checked at
each step:

1. **Add the stack state + dispatch scaffolding** but keep emitting the
   current inlining; assign resume ids without using them. No behaviour
   change. (Build green; baseline unchanged.)
2. **Switch `ONCE` to the push/goto/return primitive**; add the
   per-paragraph end checks and the dispatch switch. Keep `TIMES` /
   `UNTIL` / `VARYING` wrapping the new primitive. Re-baseline; the 11
   `--unwind`-only programs should be unchanged, the 33 still verify.
3. **Remove the `inlining` set + `assume(false)`** path. Add regression
   tests: the recursive-countdown (now `WS-ACC = 5` SUCCESS *and*
   `WS-ACC = 999` FAILS under `--unwind`), `PERFORM THRU` with
   `GO TO …-EXIT`, and a mutually-recursive error-handler cycle.
4. **Tune the default unwind interaction**: document that recursive
   performs need `--unwind`; confirm the corpus baseline and suite.

Risks and mitigations:

- *Path/state-space cost of the dispatch loop.* The added back-edges
  mean more unwinding. Mitigation: paragraphs that are never a `PERFORM`
  target and contain no `GO TO` into them need no end-check (static
  analysis); the dispatch only needs cases for reachable resume ids.
- *Perform-stack depth bound.* Too small a `D` makes deep (non-buggy)
  recursion assert-fail. Mitigation: make `D` configurable; report
  overflow distinctly from user assertions.
- *Source locations / traces.* Keep each paragraph's statements
  contiguous and labelled so counterexample traces still read in
  paragraph order.

---

## 6. Open questions

- **Stack representation**: the implementation uses CBMC's own call stack
  (one re-entrant `$proc`), so no explicit perform stack is needed; depth
  is bounded by `--unwind` like any recursion. (The §3 fixed-array stack
  is therefore moot in the shipped design.)
- **Sections** (`PERFORM section-name`): *implemented*. A section is the
  range `[section-header … last-paragraph-before-the-next-section]`;
  paragraphs carry an `is_section` flag and `range_end()` returns the
  section's last paragraph, so `PERFORM section` reuses the
  `$proc(start, end)` range mechanism unchanged.
- **Well-structured-subset performance**: *partly addressed*. Rather than
  a separate per-paragraph-function path (§4), the entry dispatch and the
  per-paragraph end checks are emitted only for the indices that are
  actually PERFORM range starts / ends (`collect_perform_targets`), so a
  program with few PERFORM targets pays almost no dispatch overhead.
- **Remaining**: `EXIT PERFORM` / `EXIT PERFORM CYCLE` (inline-PERFORM
  early exit, COBOL 2002+) and `EXIT SECTION` / `EXIT PARAGRAPH` are not
  yet modelled (currently `EXIT` is a no-op); absent from the CardDemo
  corpus, tracked for later.
