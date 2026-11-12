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

## How to repeat this

```sh
metrics/solver_bench.sh MODEL.gb LABEL [cbmc flags...]
```

Run it on any artefact that takes more than a minute, before deciding the
backend. The script checks verdicts as well as times, so a backend that
disagrees shows up as a discrepancy rather than a speedup.
