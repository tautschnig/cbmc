# Where the time goes: process metrics for the driver-proof work

**Date:** 2026-11-11
**Status:** instrumentation is now in place and collecting. The retrospective
figures below are reconstructed from evidence that happened to survive, and
each is labelled with how it was derived and what it cannot tell you.

---

## 1. Read this first: what is measured and what is reconstructed

I did not instrument this work from the start, so the two halves of this
document have very different standing.

| | basis | trust |
|---|---|---|
| §3 retrospective | git commit timestamps, retained CBMC logs, `measure_phases.sh` output, commit bodies | elapsed spans and counts are real; any split of *my* time between activities is an estimate and is marked as such |
| §4 onward | `metrics/` instrumentation, one CSV row per tool invocation | measured |

Two things the retrospective genuinely cannot distinguish:

* **Elapsed time is not work time.** A 173-hour span across the TX induction
  includes overnight background verification, waiting on 40-minute solver
  runs, and gaps where I was not working on it.
* **"Time spent finding invariants" was never separable from "time spent
  triaging a failed run".** In practice they are the same activity: you learn
  the missing clause *from* the failure. The instrumentation keeps them as
  separate categories so the next engagement can test whether that is true.

---

## 2. What is now collected

`proof/metrics/` with four files and three scripts:

| file | one row per | columns |
|---|---|---|
| `cbmc-runs.csv` | CBMC invocation | timestamp, label, step (`final`/iteration), wall, CPU, peak RSS, verdict, failing properties, VCCs generated and remaining, SSA variables, clauses, symex / SSA-conversion / solver seconds |
| `tool-runs.csv` | `goto-cc`, `goto-instrument`, `goto-analyzer` invocation | timestamp, tool, label, wall, exit code |
| `activity.csv` | change of activity | timestamp, category, subject, note |
| `iterations.csv` | proof artefact | CBMC runs to final, invariant versions, harness errors found |

**How the tool capture works, and why it needs no runner edits.** Every runner
takes its tool directory from `$BIN` or `$B`. `metrics/bin/` holds a logging
`cbmc` wrapper and symlinks for the other tools, so

```sh
M=$PWD/metrics/bin
BIN=$M B=$M ./run_all.sh
```

instruments all 24 runners. The wrapper adds `--verbosity 9`, which is what
makes CBMC print its per-phase `Runtime ...:` lines and VCC counts; it only
adds output and does not change what is verified.

`metrics/journal.sh CATEGORY SUBJECT [note]` logs an activity change;
durations are derived from consecutive events. Categories: `READ`, `HARNESS`,
`INVARIANT`, `TRIAGE`, `TOOL`, `DOC`, `WAIT`.

`metrics/report.sh` prints the breakdown: totals, per-artefact runs and
verdicts, the phase shares, final-run versus iteration time, other-tool time,
and the activity split.

Two deliberate choices. `step` distinguishes a **final** run from an
**iteration** run, because "how much time is in the final runs" was one of the
questions and it is otherwise unrecoverable. `verdict` records `NO_VERDICT`
and `OOM` separately from `FAILED`, because a timeout and a counterexample
mean entirely different things for process accounting.

---

## 3. Retrospective: what the surviving evidence shows

### 3.1 Shape of the engagement

89 commits over **186 hours elapsed** (7.8 days), derived from first and last
commit timestamps on `integration/linux`.

| work item | commits | elapsed span |
|---|---|---|
| TX induction (setup, start, xmit, cleanup, stop, `prepare_tx`, doorbell) | 30 | 173 h |
| FreeBSD side | 9 | 90 h |
| device-trust findings F1–F9, taint analysis, census | 3 | 50 h |
| device lifecycle and async handlers | 13 | 38 h |
| RX path | 9 | 16 h |
| depth (ring-size independence) | 3 | 3.3 h |
| phases 5–8 (config, leaks, DMA, concurrency) | 5 | 0.4 h |

The spans overlap — several items ran concurrently with background
verification — so they do not sum to 186.

**The visible trend is the interesting part.** The TX induction took 30
commits and the better part of a week; the RX induction, which is a comparable
artefact (six parts, its own invariants, a closed loop), took 9 commits and 16
hours. Phases 5–8 — four Unveil classes and two findings — took 5 commits in
under an hour of elapsed time. Almost all of the cost was building the first
one: the invariant vocabulary, the hygiene checks, the runner pattern and the
trap list. Transfer within the same driver is roughly **10× cheaper** than the
first path.

### 3.2 Iterations before a usable result

Reconstructed by grepping commit bodies, since I recorded each correction
there. This counts *documented* errors, so it is a lower bound.

| kind of error found before the result stood up | commits recording it |
|---|---|
| the run was vacuous — every obligation passed and a witness did not fail | **21** |
| a wrong diagnosis I had already written down, corrected after measuring | 6 |
| a path cut by a bodyless callee | 4 |
| verifying a stale binary after a silent build failure | 4 |
| an unsatisfiable entry state (contradictory assumptions) | 2 |
| a circular assertion (I forced the condition I then asserted) | 1 |
| interleaving granularity too coarse to express the race | 1 |
| **total commits documenting a self-correction** | **34 of 89** |

So **more than a third of the commits exist because an earlier attempt was
wrong in a way the tool did not report as a failure.** That is the central
process fact of this engagement, and it is why §4 of the methodology note
treats the five hygiene checks as non-negotiable rather than good practice.

For the three artefacts where I can count attempts directly from the
narrative:

| artefact | attempts before the committed result | what each attempt died of |
|---|---|---|
| TX stop step | 4 | witness placed in the wrong function; wrong `static inline` diagnosis; bodyless `ena_com_destroy_io_queue` cutting the path; free binding |
| device lifecycle | 3 | vacuous (contradictory entry state); secondary-process early return; bodyless admin teardown |
| cleanup at larger rings | 4 | loop contract not applied to the right loop; unbounded inner loops; invariant too weak; `uint16_t` wrap in the accounting clause |

### 3.3 Invariant versions

`ena_inv.h` grew to 9 predicates. The ones that changed after first use, and
why — this is the clearest answer available to "how many iterations to arrive
at the final invariant":

| predicate | versions | what forced each change |
|---|---|---|
| `INV_STARTED` | 3 | added the SQ-depth clause; pinned the placement mode after base case and step proved different modes |
| `INV_FREE_LIST` | 3 | added per-slot descriptor bounds; read `tx_descs` modulo 2^16 after the real `nb_hw_desc` turned out to go negative; bitmask restatement for cost |
| `INV_RX_STARTED` | 2 | added the accounting clause tying ring in-flight to SQ used entries, after the refill proof failed |
| `INV_CLEAN` | 2 | added the no-wrap descriptor bound after part 3 failed on `INV_STARTED` |
| `INV_STARTABLE` | 1 | — |

**Every one of those changes was forced by a failed proof, not by inspection.**
That is the strongest argument for writing the invariant early and letting the
prover tell you what is missing.

### 3.4 Where CBMC's own time went

From the 56 retained `measure_phases.sh` runs. These are final or
near-final runs of the heavier configurations, so they skew expensive.

Representative final runs:

| configuration | wall | symex | SSA conversion | solver | clauses |
|---|---|---|---|---|---|
| xmit step, ring 8, burst 2 | 538 s | — | — | — | 14.7 M |
| LLQ end-to-end, `ASSUME_NO_F14` | 1059 s | 531 s | 30 s | 486 s | 75.3 M |
| TX stop step, ring 8 | 22 s | 17 s | 0.7 s | 0.25 s | 0.2 M |
| cleanup, ring 8, cheap free list | timeout at 2800 s | 27 s | 4.4 s | — | 10.7 M |

The pattern across them: **symex and solver are each roughly half**, and the
balance flips with the formula size. Below ~1 M clauses symex dominates and
runs finish in seconds; above ~10 M the solver dominates and may not finish at
all. SSA conversion is consistently a few percent — which is worth saying,
because an earlier note in this project wrongly blamed it.

One measured intervention: hoisting two endianness maps out of a per-offset
loop in `convert_byte_update` took a 16 KB object from **8.79 s to 0.71 s**.
That is now a CBMC commit awaiting upstreaming.

### 3.5 What I cannot reconstruct

* The split of my own time between reading source, writing harnesses,
  devising invariants and triaging. The categories now exist; there is no
  retrospective evidence for them.
* The number of CBMC invocations per artefact. 287 log files survive but they
  are overwritten per configuration, so the count is a floor, not a total.
* Time on tools other than CBMC. The CodeQL and kernel-census work predates
  any logging.

---

## 3.6 First instrumented artefact: the xmit step-wise decomposition

The xmit depth work was measured from the start. 16 CBMC runs, 1.8 h of
solver wall time.

| question asked | measured answer |
|---|---|
| where CBMC's time goes | **solver 98%**, symex 2%, SSA conversion 0% |
| final runs vs iterations | 8 final (1.66 h), 8 iteration (0.14 h) |
| other tools | 44 invocations, 0.14 h — all of it `goto-cc` |
| runs to a usable result | 3 for part 1, 1 harness error, 1 invariant version |

Two of these correct expectations from §3.4 and §4:

* **Solver share was 98%, not "roughly half".** The retrospective figure came
  from the heavier end-to-end configurations where symex is substantial. For
  a focused step proof the formula is small but hard, so the solver dominates
  almost entirely. The lesson is that "where does CBMC's time go" has no
  single answer — it depends on the shape of the harness, and only
  per-artefact measurement tells you which lever to pull.
* **Final-run time *exceeded* iteration time, 1.66 h against 0.14 h.** I
  predicted the opposite. The reason is visible in the per-artefact table: the
  iterations were all at ring 8 and cheap (30–200 s), while the *finals* were
  the ring-16 and ring-32 scaling runs at 2774–3141 s each. So for depth work
  the prediction inverts: iterations are cheap because you debug at the small
  size, and the expensive runs are the ones that establish the reach. I would
  expect the original prediction to hold for breadth work, where each new
  artefact is debugged at its final size.

### A flaw in the activity journal, and what I did about it

The first activity report attributed **7.15 h to `TOOL`** and ~0 to reading,
harness-writing and invariant work. That is an artefact of the design, not a
finding: durations are derived from consecutive events, so an interval
containing a long background verification charges that machine time to
whatever category was logged before it.

`report.sh` now reports two columns — `gross` elapsed and `net` after
subtracting CBMC wall time that overlaps the interval — which brought `TOOL`
to 5.46 h. That is an improvement but still wrong, because overlapping
background runs and runs killed by `timeout` are not fully captured.

**So the activity split is not yet trustworthy, and should not be quoted.**
The CBMC-side numbers above are. What the journal needs is the discipline now
documented in `journal.sh`: log a `WAIT` event when launching a background
run, and another when returning to real work. Until a stretch of work is
logged that way, treat §3.5's "cannot reconstruct" as still applying to the
activity split.

## 4. What I expect the collected data to answer

Once a few more artefacts have gone through instrumented, `report.sh` will
answer directly:

* **How much time is in the final runs versus the iterations before them.**
  Answered for one artefact, and it refuted my expectation: see §3.6. The
  prediction that finals are a minority of solver time holds for breadth work
  but inverts for depth work, where the expensive runs are the ones probing
  the reach.
* **Whether invariant discovery is separable from triage.** I expect not, and
  if the `INVARIANT` and `TRIAGE` categories track each other closely that is
  worth reporting as a finding about the method.
* **The cost of the hygiene checks.** Every artefact runs a vacuity build as
  well as a proof build, so roughly double. Against 21 caught vacuous results,
  that is clearly worth it, but it should be quantified rather than asserted.
* **Where the per-artefact cost sits now that the pattern is established.** If
  the RX-induction ratio holds, a new operation on a known driver should cost
  a small number of runs and a couple of hours.

## 5. How to use this on the next driver

Start the instrumentation on day one:

```sh
M=/path/to/proof/metrics/bin
export BIN=$M B=$M                 # every runner is then logged
metrics/journal.sh READ foo.c "entry points"
...
metrics/report.sh
```

and set `METRICS_STEP=iteration` for exploratory runs so the final-versus-
iteration split is real. Record an `iterations.csv` row per artefact when you
commit it, with the invariant-version count — that number is the one this
retrospective most wishes it had.
