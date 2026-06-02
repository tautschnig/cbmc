# Why isn't this tooling finding new bugs? — a retrospective

**Date:** 2026-06-02
**Status:** Honest methodological retrospective.  No new code;
this is the "step back and ask why" the effort needs.

## The uncomfortable observation

We have built a lot:

* 35 property modules (refcount/lifetime, leak-on-error,
  use-after-free, null-after-alloc, lock balance,
  cancel-work-before-free, netlink validation, integer
  overflow, TOCTOU, uninit-to-user, …).
* A per-file CBMC harness pipeline (compile-stack +
  Coccinelle instrumentation + harness synthesis + contract
  replacement + drop-unused + cbmc).
* A triage filter, an FP-measurement harness (n=500/n=1000),
  multi-LTS validation, a result cache, SARIF export.
* A cross-function destructor-completeness detector and a
  whole-kernel batch scanner for it.
* 43 design/closeout docs.

And across **two independent approaches** — the CBMC corpus
scan (n=1000, ~1% post-triage candidate rate) and the static
destructor scan (whole-kernel, 66–88 candidates/tree) — the
number of **confirmed, previously-undisclosed bugs found is
zero.**  Every CVE we "detect" is a historical, already-fixed
one we reverse-engineered detection for.

The user's question is the right one: *the kernel is ~30M
lines; it is statistically certain to contain thousands of
live bugs.  So either we cannot detect the shapes of the bugs
that are present, or we are not looking where they are.*

The honest answer is **both — plus a third reason we don't
usually say out loud.**  None of them is a coding defect in
the tooling; they are structural.

## Reason 1 — We target commodity shapes that incumbent tools already sweep continuously

Look at the catalog: it is overwhelmingly **memory-safety and
object-lifetime** shapes — refcount imbalance, leak on error
path, use-after-free, null-after-alloc, missing cleanup.

These are exactly the shapes that the kernel's *existing*
toolchain hunts **continuously and pre-merge**:

* **Coverity** scans mainline daily (Coverity Scan).
* **Smatch** and **sparse** run on most subsystem maintainer
  trees; Smatch in particular specialises in
  leak/error-path/refcount checking.
* **syzkaller** fuzzes mainline 24/7 with **KASAN**,
  **KMEMLEAK**, **KCSAN**, **UBSAN** enabled — catching
  use-after-free, leaks, and races dynamically.
* **smatch/coccinelle** semantic patches gate many of these
  patterns at review time.

The residual density of *these specific shapes* in **mainline
/ LTS** is therefore near-zero: the easy ones are caught
before merge or fixed within days.  We are scanning the
most-scrutinised C code on Earth, for the patterns that code
is most scrutinised for, with a **weaker, slower magnet** than
the incumbents.  Finding a *new* instance of a shape that
five mature tools already hunt is the exception, not the rule.

Evidence from our own runs: the destructor-scan candidates
cluster in **well-maintained** subsystems (infiniband, fs,
kernel/, net) — not in staging or obscure drivers.  In
heavily-maintained code, a surviving candidate is almost
always a false positive, because the real instances were
already found.  That is exactly what our triage showed.

## Reason 2 — We look in already-swept places, uniformly

We scan **mainline/LTS snapshots, whole-file, uniformly.**
But residual bugs do not distribute uniformly; they
concentrate in:

* **New code** — commits that have not yet been through a
  Coverity/Smatch/syzkaller cycle.  The window between "merged"
  and "swept" is where fresh bugs live.
* **Rarely-built configurations** — code behind unusual
  Kconfig combinations that the bots don't compile, so static
  and dynamic tools never see it.
* **Staging / obscure / vendor drivers** — `drivers/staging`,
  niche hardware, and especially **out-of-tree / vendor BSP**
  code, which receives a tiny fraction of mainline's scrutiny.
* **Deep error paths** — reachable only under allocation
  failure or device-error injection, which fuzzers exercise
  poorly and which we, scanning a static snapshot uniformly,
  do not prioritise.

We target none of these preferentially.  We point the tool at
`linux_5_10`/`6_1`/`6_6`/`6_12` HEADs and scan everything the
same way.  That is the equivalent of looking for lost keys
under the streetlight.

## Reason 3 — Validating on historical CVEs overfits to "detectable in hindsight"

Every CVE in our catalog is, by construction, a bug that was
**found, reported, and fixed.**  We then reverse-engineer a
detector that fires on it.  This measures *detectability in
hindsight*, not *discovery power*.  It is a closed loop:

> pick bugs that were already detectable → build detectors
> that detect them → report high recall on them.

Our "7/10 recall" is recall **on the already-detected
subset**.  It says almost nothing about the undetected bugs,
which by definition do not match the shapes we tuned to the
known ones.  Worse, the aggressive **triage filtering** we did
to push the FP rate to ~1% was calibrated against *known FP
shapes* — which also suppresses any genuine bug that happens
to share a surface shape with a common FP.  We optimised for
precision on a historical benchmark, which is textbook
overfitting.

## The deeper problem — we are using a theorem prover to do a linter's job

This is the most important point.

CBMC is a **bounded model checker.**  Its unique strength is
**precise, exhaustive reasoning about arithmetic, bit
operations, array indices, and small-bounded path
enumeration** — properties that pattern-matchers *cannot*
decide because they require actually computing whether
`a * b + c` can overflow and index out of bounds.

We pointed it at **object-lifetime / leak** bugs — refcount
balance, `kfree` pairing, ownership transfer.  Those are
**typestate** properties.  A flow-sensitive pattern-matcher
(Smatch, Coverity's checkers) decides typestate cheaply across
the whole tree.  CBMC, forced through a per-file harness that
**havocs everything outside the function** and bootstraps a
synthetic ghost, is a slow, low-coverage way to ask a question
a linter answers better and at scale.

We brought a SAT solver to a grep fight — on grep's home turf.

## What would actually find bugs (concrete pivots)

In rough order of expected yield:

### A. Differential / PR-scanning (regression mode) — highest yield
Stop asking "is this function buggy?" and ask **"does this
*patch* introduce a contract violation?"**  Scan the **delta**
of recent commits / a patch series, *before* the incumbent
tools sweep it.  This wins on three axes at once:

* **New code** (Reason 2): you're looking where fresh bugs are.
* **First-look advantage**: you're scanning before Coverity's
  next daily run.
* **Narrow scope**: a 200-line diff is tractable for CBMC's
  precise reasoning in a way a whole subsystem is not, and the
  before/after comparison gives a clean signal (property held
  before, violated after).

This reframes the methodology from *discovery in a swept
haystack* to *regression-gating fresh changes*, which is both
higher-yield and a defensible product story.

### B. Play to CBMC's actual strength — arithmetic / bounds
Target the shapes a pattern-matcher *cannot* decide:

* integer overflow feeding an **allocation size** or **array
  index** (`integer_overflow_in_alloc_size`,
  `copy_from_user_size_check` — modules we built but barely
  exercised),
* off-by-one and shift/mask errors in bounds math,
* length/offset validation in parsers (netlink, ASN.1,
  filesystem on-disk structures) where the property is "this
  computed offset stays in bounds for all inputs."

These need exhaustive bounded reasoning, which is CBMC's
comparative advantage.  This is where a model checker can find
what linters miss.

### C. Target unswept code
Run the existing detectors against **out-of-tree / vendor BSP
trees, `drivers/staging`, and very recent commits** — code
that has *not* had years of Coverity/Smatch/syzkaller
attention.  Same detectors, residual-bug-rich haystack.  This
is the cheapest pivot: no new detector work, just point the
tool somewhere unswept.

### D. Concurrency (large investment)
A large fraction of serious residual kernel bugs are **races**
(refcount races, use-after-free under concurrent teardown,
missing-barrier publish).  CBMC has some concurrency support,
but our harness models a single thread.  This is the highest-
ceiling and highest-cost direction; only worth it as a
dedicated project.

## Honest reframing of the value proposition

If whole-kernel mainline scanning for commodity shapes yields
zero new bugs, that is **the expected result, not a failure** —
and the infrastructure still has real value if we stop
claiming the wrong thing for it:

1. **Regression prevention / CI gating.**  The catalog is a
   precise, per-property checker that can sit in a subsystem's
   CI and catch *reintroduction* of a known bug shape (a
   refcount imbalance, a leak on a new error path) in a
   specific, security-critical function.  That is a real,
   defensible use — "this function provably still satisfies
   its contract after your change."
2. **Precise verification of designated functions.**  For a
   hand-picked security-critical routine (a parser, a
   permission check, an allocation-size computation), CBMC can
   *prove* a property holds for all bounded inputs — something
   no linter or fuzzer does.  Value per function, not bugs per
   kernel.

Both are legitimate.  Neither is "we scan the kernel and find
new CVEs," which the evidence says we should stop promising.

## Recommendation

1. **Stop** running whole-kernel uniform scans of mainline/LTS
   for commodity memory-safety shapes expecting new finds.
   Record that as a deliberate negative result (this document).
2. **Pivot to differential PR-scanning (A)** as the primary
   bug-finding strategy, and **point the existing detectors at
   unswept code (C)** as the cheap parallel experiment.
3. **Refocus new detector work on CBMC's arithmetic/bounds
   strength (B)**, reviving the integer-overflow and
   size-check modules with real targets (parsers, ioctls,
   on-disk/​wire-format validation).
4. **Reframe the catalog's headline value** as regression-
   prevention + precise per-property verification, and report
   recall honestly as "recall on the historical-CVE subset",
   not as a discovery claim.

The single most likely path to an actual new-bug find is **A
on a C-target**: differential scanning of a recent patch
series or a less-swept (vendor/staging) tree, focused on a
shape CBMC is uniquely good at.  Everything we have built —
the harness, the contracts, the triage, the cache — is
reusable for that; what changes is *where we point it and what
question we ask.*

## Cross-references

* `methodology-2026-05.md` — the methodology and the honest
  recall numbers (7/10 on the historical subset).
* `destructor-bug-hunt-2026-06.md` — the whole-kernel
  destructor hunt (0 confirmed new bugs; FP-mechanism
  characterisation) that prompted this retrospective.
* `bug-hunt-fp-shapes-2026-05.md` — the earlier n=500 long-tail
  triage (also 0 real bugs), consistent with Reason 1.
