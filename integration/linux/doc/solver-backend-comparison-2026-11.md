# Which solver backend, and does it matter? A measurement

**Date:** 2026-11-12
**Prompted by:** solving being 98% of CBMC's time on the step proofs
(`proof-process-metrics-2026-11.md` §3.6), which makes the backend the
obvious lever.
**Script:** `proof/metrics/solver_bench.sh`, raw data in
`proof/metrics/solver-bench.csv`.

## Headline

**`--sat-solver cadical`, which every runner here has been using, is not a
win and on one artefact is a 4.5× loss.** MiniSat2 (CBMC's default) and
Glucose are consistently the fastest or tied-fastest. The exception is the
single longest-running artefact, where CaDiCaL is 4% ahead.

Every backend that produced a verdict **agreed** on it, including the
property counts, so none of this is a soundness question.

## The numbers

Same goto-binary, same flags, only the backend varying. Wall seconds.

| artefact | properties | minisat2 | cadical | glucose | bitwuzla | z3 | cvc5 |
|---|---|---|---|---|---|---|---|
| `aenq`, depth 4 | 11808 | **7.2** | 8.4 | — | 43.3 | 8.9 | 26.5 |
| `xmit_step` p1, ring 8 | 7689 | **37.8** | 169.9 | 37.6 | 65.3 | crash | crash |
| `cleanup_step` p2, ring 8 | 6709 | 14.0 | 22.2 | **13.6** | 20.8 | — | — |
| `rx_recv`, ring 4 | 6895 | 585.0 | **560.0** | 593.5 | — | — | — |

Reading it:

* **CaDiCaL loses on three of four**, by 1.2×, 4.5× and 1.6×, and wins the
  fourth by 1.04×.
* **MiniSat2 and Glucose are within noise of each other** everywhere.
* **Bitwuzla** (SMT, bit-blasting) works and agrees, but is 1.5–6× slower.
* **The ranking is not stable across artefacts.** The 4.5× gap on the xmit
  step and the 4% gap the other way on `rx_recv` are the same two solvers.
  There is no single right answer, which is the main practical lesson.

## Z3 and CVC5 abort on these models

Both die with an internal `map::at` during SMT2 conversion:

```
Passing problem to SMT2 QF_AUFBV using Z3
converting SSA
map::at
```

They work on the smaller `aenq` model and fail on the xmit one, so it is
model-dependent rather than a flat incompatibility. This is a CBMC bug in the
SMT2 backend, not a configuration mistake, and is worth a minimal reproducer
and an upstream report. It is unrelated to the two commits already queued for
upstreaming.

## What this changes

1. **Stop hard-coding one backend.** The 34 runners pin
   `--sat-solver cadical`. That should become a variable with MiniSat2 as the
   default, and the expensive artefacts should be benchmarked individually.
2. **The ring-32 negatives were measured with CaDiCaL only.** Every "no
   verdict in N seconds" recorded in these notes — xmit at ring 32, cleanup at
   ring 64 — used the slower backend on at least some artefacts. Those
   negatives are therefore weaker than stated and should be re-measured with
   MiniSat2 before being treated as limits.
3. **It does not rescue the xmit scaling.** A 4.5× speedup against an ~18×
   cost per ring doubling moves the reach by well under one doubling, so the
   conclusion in `ena-cleanup-ring8-2026-11.md` — that a decomposition removes
   only the bound its own loop imposes — stands.

## An instrumentation bug this exposed

Chasing the ring-32 retest produced no data at all, which turned out to be my
own fault: the runners wrap `$BIN/cbmc` in `timeout`, so `timeout` kills the
**logging wrapper** rather than the real solver underneath, and the wrapper
died before writing its CSV row. **Every run that hit its timeout was silently
absent from the metrics** — precisely the set most worth recording.

Fixed: the wrapper now traps `TERM`/`INT`, kills its child, writes a
`TIMEOUT` row with the elapsed time, and exits 124. Verified in both
directions — a timed-out run logs `TIMEOUT`, a normal run still logs the full
phase breakdown, and no orphan solver is left behind.


## SMT2 backends: works, fails, CPU and memory

Measured across five models and eight backends (45 runs), with CPU and peak
RSS from `/usr/bin/time` so the SMT2 child processes are included. Raw data in
`proof/metrics/solver-bench.csv`.

### Does it work at all

| backend | kind | successful | crash | cannot decide | timeout |
|---|---|---|---|---|---|
| minisat2 | SAT | 5 | – | – | – |
| cadical | SAT | 5 | – | – | – |
| glucose | SAT | 5 | – | – | – |
| **bitwuzla** | SMT2 | **6** | – | – | – |
| z3 | SMT2 | 1 | 4 | – | 1 |
| `--smt2` (default Z3) | SMT2 | 1 | 4 | – | 1 |
| cvc5 | SMT2 | 1 | 4 | 1 | – |
| cprover-smt2 | SMT2 | – | – | 6 | – |

**Only one SMT2 backend is usable on this code: Bitwuzla.** It solved
everything put to it and agreed with the SAT backends every time. The reason
it differs from the others is that `--bitwuzla` goes through CBMC's native API
binding, while `--z3`, `--cvc5` and `--smt2` go through the SMT2 *text*
writer — and that writer is where the bug is.

* **Z3, CVC5 and `--smt2` crash with `map::at`** on four of five models. They
  succeed only on `aenq`, the one model that does not `#include ena_ethdev.c`
  and therefore lacks DPDK's large `rte_eth_devices` arrays and anonymous
  unions. So it is a property of the model, and the failing models are the
  normal case for driver work.
* **`cprover-smt2` decides nothing on any model.** It returns `ERROR` for the
  byte-extract properties over the driver's anonymous unions — the same
  limitation that makes `--pointer-check` report ERROR there — so every run
  came back `VERIFICATION ERROR`.

### Cost, where a verdict was reached

| model | props | minisat2 | cadical | glucose | bitwuzla | z3 |
|---|---|---|---|---|---|---|
| `freelist_eq` ring 4 | 7128 | 2.7 s | 2.5 s | 2.7 s | 3.1 s | crash |
| `aenq` depth 4 | 11808 | **7.1 s** | 8.8 s | 7.1 s | 42.6 s | 8.8 s |
| `cleanup_step` p2 ring 8 | 6709 | **13.4 s** | 21.8 s | 13.4 s | 20.9 s | crash |
| `admin_cq` depth 4 | — | **24.0 s** | 63.4 s | 24.4 s | 304.7 s | timeout |
| `xmit_step` p1 ring 8 | 7689 | **38.4 s** | 171.4 s | 38.2 s | 64.3 s | crash |

**Bitwuzla is 1.1–12.7× slower than MiniSat2** where both finish, worst on
`admin_cq`. So it is a usable cross-check, not a faster option.

### Memory

Worst-case peak RSS per backend across the five models:

| backend | peak RSS |
|---|---|
| z3 / cvc5 / `--smt2` | **2409 MB** |
| bitwuzla | 1522 MB |
| cadical | 770 MB |
| minisat2 / glucose | 432 MB |
| cprover-smt2 | 410 MB |

The SMT2 text backends cost **5.6× the memory of MiniSat2** and, on these
models, mostly crash before using it productively. CaDiCaL also costs 1.8×
MiniSat2's memory — consistent with it being slower here rather than trading
memory for speed.

### A caveat on SMT2 CPU figures

For `z3` and `--smt2` on `admin_cq` the wall/CPU ratio is 6.8 (1800 s wall,
265 s CPU). CBMC writes an SMT2 file and invokes the solver as a separate
process, and `/usr/bin/time` on CBMC does not reliably attribute a
grandchild's CPU. **So CPU for the SMT2 text backends is an underestimate and
only their wall time and RSS should be compared.** Bitwuzla, being in-process,
reports wall and CPU within 0.1% of each other and is directly comparable.

### Conclusion for this project

* Default to **MiniSat2**; Glucose is interchangeable.
* Keep **Bitwuzla** as an independent cross-check on any result that matters,
  since it is a genuinely different decision procedure that agreed on all six
  runs. It costs several times the time and memory, so use it on final runs
  only.
* **Do not use the SMT2 text backends on driver models** until the `map::at`
  bug is fixed.
* The `map::at` crash and the `cprover-smt2` anonymous-union `ERROR` are both
  worth minimal reproducers and upstream reports, separate from the two
  commits already queued.

## How to repeat this

```sh
metrics/solver_bench.sh MODEL.gb LABEL [cbmc flags...]
```

Run it on any artefact that takes more than a minute, before deciding the
backend. The script records outcome, wall, CPU and peak RSS, and classifies
non-verdicts as `CRASH`, `ERROR`, `UNSUPPORTED`, `TIMEOUT` or `OOM` rather
than lumping them together — the distinction matters, as the SMT2 results
above show. Verdicts are recorded too, so a backend that disagrees shows up as
a discrepancy rather than a speedup.

`BACKENDS` overrides the list; spaces inside an option are written as `~`,
because the list is word-split. (The first version of the script did not do
this, and the resulting three bogus `CRASH` rows for the SAT backends were
purged from the CSV once identified.)
