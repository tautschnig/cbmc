# CVE-catalog gaps vs CBMC's strengths, and a CodeQL→CBMC refinement design

**Date:** 2026-06-02
**Status:** Gap analysis + architecture proposal (no new
detector code yet — this answers "what should we build and
why").

Two questions, answered against the
`cve-survey-2023-2026.md` evidence base:

1. Is there a CVE class we **don't** cover that **better fits
   CBMC's strengths**?
2. Can we use an **over-approximating** analysis (CodeQL) to
   propose candidates and **CBMC to filter the false
   positives**?

The answers are connected: the same class answers both.

## Q1 — the catalog is skewed *away* from CBMC's strengths

CBMC is a bounded model checker.  Its native, default-on
checks are exactly the **arithmetic / memory-safety** ones:

```
--bounds-check (default on)   array index in bounds
--pointer-check (default on)  pointer offset valid
--signed/unsigned-overflow-check   integer overflow
--div-by-zero-check           division
--conversion-check            truncation
```

These decide properties a flow-sensitive pattern-matcher
**cannot**: whether `a*b+c` overflows, whether a computed
index can exceed an array bound *for some input*.

But our catalog is ~75% **typestate / lifetime / ordering**
modules — the shapes a pattern-matcher (Smatch, Coverity)
decides *better* than CBMC:

| Catalog skew | modules | CBMC fit |
|---|---|---|
| refcount / lifetime / UAF / lock / RCU / ordering / concurrency | ~25 (cred, refcount, kobject, device, of_node, inode, dentry, fput, sock, skb, module, kref, lock_state, rcu_critical_section, concurrent_*, cancel_*_before_free, del_timer_*, use_after_free_generic, page_provenance, alloc_tag, permission_bypass, tocttou) | **weak** (typestate — linter's job) |
| arithmetic / bounds / taint | ~8 (integer_overflow_in_alloc_size, copy_from_user_size_check, division_by_zero_check, netlink_attr_validation, scatterlist, uninit_to_user, format_string, aead) | **strong** |

Cross-referencing the survey's category sizes against CBMC
fit reveals the gap precisely:

| Category | CVEs | % | CBMC fit | Our coverage |
|---|---:|---:|---|---|
| **out_of_bounds** | **649** | **7.4%** | **native (`--bounds-check`)** | **only slivers** (netlink attr, AEAD sgl, copy_from_user) |
| integer_overflow | 122 | 1.4% | native (`--overflow-check`) | alloc-size subset only |
| dos_panic_warn (÷0 subset) | 249 | 2.9% | native (`--div-by-zero-check`) | division_by_zero ✓ |
| string_or_copy_bound | 27 | 0.3% | native | copy_from_user ✓ |
| uninit_or_info_leak | 44 | 0.5% | partial | uninit_to_user ✓ |

**The headline gap: `out_of_bounds` is the third-largest CVE
category (649, 7.4%) and is CBMC's single defining
capability, yet we have no general array-index / computed-
offset bounds module.**  We built 11 refcount-lifetime modules
(typestate — CBMC's *weakness*) and zero general-bounds
modules (CBMC's *strength*).  That is the mismatch to fix.

Most valuable specific shape — and the one
pattern-matchers fundamentally *cannot* decide — is the
composite chain:

> **tainted integer → arithmetic (overflow/scale) → array
> index or copy length → OOB**

CodeQL/Smatch can see the *taint reachability*; only a model
checker can decide whether the *arithmetic actually escapes
the bound* on a feasible path.

## Why a uniform CBMC bounds-scan fails — and why that forces Q2

We can't just turn on `--bounds-check` and scan every
function.  In a per-file harness the index/length/size inputs
are **havoc'd nondet**, so CBMC dutifully reports "index could
be 2^31 → OOB" for almost every access — vacuous FPs at
massive scale.  To use CBMC's bounds strength productively it
needs **context**: which inputs are actually attacker-
controlled, and which bounding checks exist on the path.

That context — *taint reachability minus sanitizers* — is
exactly what an over-approximating dataflow engine computes.
So Q1 (the OOB gap) and Q2 (CodeQL front-end) are the same
plan.

## Q2 — CodeQL (over-approx) → CBMC (refute) is the right architecture

This is a classic **static-triage + precise-refutation**
(CEGAR-flavoured) pipeline, and OOB/overflow is the ideal
target because the two tools' strengths are **complementary
and non-overlapping**:

| Stage | Tool | Strength | Weakness it delegates |
|---|---|---|---|
| 1. propose | CodeQL | whole-program taint/dataflow reachability, high recall | can't decide if the value is *actually* bounded → many FPs |
| 2. refute | CBMC | precise arithmetic + path feasibility, gives a concrete witness | can't scale whole-program reachability |

**Division of labour for the OOB chain:**

* **CodeQL (stage 1):** source = user/attacker-controlled
  value (`copy_from_user`, `get_user`, `nla_get_*`, ioctl
  arg, sysfs/debugfs write, on-wire packet field); sink =
  array subscript, `memcpy`/`memmove` length, or allocation
  size; barrier = a comparison that bounds the value.  Report
  flows source→sink with **no** barrier.  This over-
  approximates: it flags every *reachable* flow, including the
  many where an implicit or non-obvious bound makes it safe.
* **CBMC (stage 2):** for each CodeQL candidate, synthesise a
  harness that makes the identified source `nondet`, replays
  the enclosing function (with the preceding branch
  conditions as `__CPROVER_assume`), and runs default
  `--bounds-check`/`--*-overflow-check`.  CBMC then:
  * finds a concrete input that drives the index/length past
    the bound → **confirmed true positive, with a triggering
    input** (the highest-value output the whole effort can
    produce); or
  * proves no input does → **CodeQL false positive, filtered
    out.**

This directly dissolves the FP problem that sank the uniform
scan: CodeQL says *where* to look (tainted → unbounded sink),
CBMC supplies the *proof*, and the residue is concrete bugs
with witnesses.

It also composes with the A+B+C pipeline: run stage 1 over
**fresh** code (A) in a **less-swept** subtree (C); the
property is **arithmetic/bounds** (B).

## Practicality / cost

* **CodeQL is not installed here** (`codeql` not on PATH), and
  building a CodeQL database for the Linux kernel is a heavy
  operation (full extraction during a kernel build — hours,
  tens of GB).  That is a real commitment, not a quick
  experiment.
* **We can validate the architecture without CodeQL first.**
  Stage 1 only needs to be *over-approximating*; we already
  have lighter over-approximators — the Coccinelle prefilters
  (`copy_from_user_size_check.cocci`,
  `integer_overflow_in_alloc_size.cocci`) and the
  `diff_arith_scan.py` selector — that flag the *sink* side
  cheaply.  Pairing those (stage 1') with a CBMC bounds
  harness (stage 2) is a faithful, buildable prototype of the
  two-stage design.  Graduate stage 1 to CodeQL's real
  taint-tracking once the architecture proves its yield.

## Recommendation

1. **Build the general `array_bounds` / `size_overflow_to_oob`
   property module** — the biggest CBMC-fit gap (649-CVE
   category, native `--bounds-check`).  Frame it as a
   *refinement* check (assume path conditions, havoc the
   tainted source, assert in-bounds), not a standalone scan.
2. **Stand up the two-stage refinement pipeline** with our
   existing prefilters as stage 1', CBMC as stage 2, on the
   A+B+C candidate set.  Measure yield (confirmed OOB with
   witness vs FPs filtered).
3. **Only then invest in a CodeQL kernel database** to replace
   stage 1' with industrial taint-tracking, if the prototype
   shows the refinement step earns its keep.

This is the most defensible path to an actual new-bug find:
it targets the largest category that is *also* CBMC's core
competency, and it uses CBMC for the one thing it does better
than every incumbent — turning an over-approximate candidate
into a proof with a concrete triggering input.

## Cross-references

* `cve-survey-2023-2026.md` — category sizes / coverage audit.
* `why-no-new-bugs-retrospective-2026-06.md` — why typestate
  scanning of swept mainline yields nothing (pivot B).
* `combined-abc-pipeline-2026-06.md` — the A+B+C selector this
  refinement pipeline consumes.
