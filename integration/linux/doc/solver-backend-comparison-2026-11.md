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
everything put to it and agreed with the SAT backends every time.

**Why, corrected.** An earlier version of this note said Bitwuzla uses a
native in-memory API while the others go through the SMT2 text writer. That is
wrong. All of them use the same text writer: `smt2_dec.cpp:73-95` writes the
problem to a temporary file and invokes the solver binary as a child process,
Bitwuzla included (`:77`). There is no in-memory interface.

The real difference is the **per-solver encoding** chosen in
`smt2_conv.cpp:78-125`:

| backend | `use_datatypes` |
|---|---|
| Z3 | **true** (`:125`) |
| CVC5 | **true** (`:110`) |
| Bitwuzla | not set (`:84-91`) |
| cprover-smt2 | not set (`:96-103`) |

and `map::at` is `datatype_map.at(type)` in `type2id`
(`smt2_conv.cpp:1170`), reached for a `struct_tag` **only when
`use_datatypes` is set**:

```cpp
else if(type.id() == ID_struct_tag)
{
  if(use_datatypes)
    return datatype_map.at(type);     /* throws when type is unregistered */
  else
    return "S" + std::to_string(boolbv_width(type));
}
```

So Bitwuzla is not more robust — **it simply never exercises the datatype
encoding**, because CBMC does not enable datatypes for it. The bug is in the
struct/datatype encoding path, and any backend configured with
`use_datatypes` would hit it. That is a more useful conclusion than the one it
replaces: it localises the defect rather than crediting a solver.

`cprover-smt2` also leaves `use_datatypes` unset, so its failure is unrelated:
it returns `ERROR` for the byte-extract properties over the driver's anonymous
unions, the same limitation that makes `--pointer-check` report ERROR there.

**I could not reduce the crash to a minimal reproducer.** Five synthetic
shapes under `--z3` all verify normally: a self-referential struct through a
pointer, a struct with an anonymous union, one with a named union containing a
struct, one with an array of structs, and one with an array of self-pointers.
So the trigger is none of those alone. The four models that crash all
`#include ena_ethdev.c` and the one that does not (`aenq`) is the one that
survives, which points at something in DPDK's type graph, but that is a
correlation across five models and not a diagnosis. Producing a reproducer
needs reduction from a real model and is open work — stated here rather than
guessed at.

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

### A caveat on CPU figures, corrected

For `z3` and `--smt2` on `admin_cq` the wall/CPU ratio is 6.8 (1800 s wall,
265 s CPU). An earlier version of this note attributed that to CBMC invoking
the solver as a separate process whose CPU `/usr/bin/time` misses. The data
refutes that: **every completed run has a wall/CPU ratio of 1.00**, SMT2
backends included, so child CPU *is* attributed.

The 6.8 ratio appears only on the two `TIMEOUT` rows. The cause is the
timeout itself — `timeout` kills the process and the child solver is never
reaped, so its `rusage` is lost. **CPU is therefore an underestimate for
timed-out runs only**, whatever the backend, and is directly comparable
everywhere else.

### Conclusion for this project

* Default to **MiniSat2**; Glucose is interchangeable.
* Keep **Bitwuzla** as an independent cross-check on any result that matters:
  it is a different decision procedure, it agreed on all six runs, and it
  reaches CBMC through a different encoding (no datatypes). It costs several
  times the time and memory, so use it on final runs only.
* **Do not use the SMT2 text backends on driver models** until the `map::at`
  bug is fixed.
* The `map::at` crash and the `cprover-smt2` anonymous-union `ERROR` are both
  worth upstream reports, separate from the two commits already queued. The
  first now has a localised cause — an unregistered `struct_tag` in
  `datatype_map` under `use_datatypes` — but still needs a reproducer reduced
  from a real model.

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
