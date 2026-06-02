# CodeQL → CBMC two-stage refinement prototype

Validated proof-of-concept for the architecture proposed in
`../../doc/cbmc-fit-gaps-and-codeql-refinement-2026-06.md`:
an **over-approximating** static analysis (CodeQL) proposes
candidates with high recall, and **CBMC** refines each one —
confirming real bugs with a concrete witness or proving the
candidate safe (filtering the false positive).

This is the de-risking experiment: it answers *does the
refinement step give a decisive, non-vacuous verdict on a
controlled example with known ground truth?* — **yes**, with
both real tools.

## The example (`bounds.c`)

Two functions copy an **untrusted** length into a 64-byte
buffer:

* `copy_vuln` — guards against the **wrong** bound (`> 128`);
  a real OOB write when `64 < len <= 128`.
* `copy_fixed` — guards against `sizeof(dst)`; safe.

Both share the same taint shape (`untrusted_len()` → `memcpy`
size), so a pure-reachability analysis cannot tell them apart.

## Result

| Stage | Tool | `copy_vuln` | `copy_fixed` |
|---|---|---|---|
| 1 propose | CodeQL taint | **flagged** | **flagged** (FP) |
| 2 refine | CBMC `--bounds-check` | **FAILED**, witness `len=128` | **SUCCESSFUL** |

CodeQL over-reports (2 candidates); CBMC refines to exactly
the 1 real bug, with a triggering input, and proves the other
safe.  That is the whole thesis in one run.

## Reproduce

```sh
ulimit -v 16000000
./run.sh        # needs gcc, the CodeQL bundle, and build/bin/cbmc
```

Files:
* `bounds.c` — ground-truth example (+ CBMC harnesses under
  `-DCBMC_HARNESS`).
* `tainted_memcpy_size.ql` — stage-1 taint query
  (`untrusted_len()` → `memcpy` size), deliberately *without*
  guard reasoning so it over-approximates.
* `qlpack.yml` — declares the `codeql/cpp-all` dependency so
  the query resolves from the bundle (`--additional-packs`,
  no network).
* `run.sh` — drives both stages.

## What this validates — and what it does not

**Validates:** the refinement mechanic.  Marking the tainted
source `nondet` and letting CBMC carry the function's own
guard (`if(len > ...)`) yields a decisive verdict: a witness
for the real bug, a proof for the FP.  This is exactly the
property the uniform-scan approach lacked (havoc'd inputs gave
vacuous "index could be huge" FPs).

**Does not yet validate:** the kernel-scale path.  Two gaps
remain before this finds a real kernel bug:

1. **Stage-1 on the kernel** needs a CodeQL database built by
   observing a kernel compile (heavy: full extraction during a
   `make`, hours / tens of GB).  The CLI + C/C++ packs are
   installed; the DB build is the outstanding cost.
2. **Stage-2 harnessing of a real kernel function** must
   reproduce the *relevant* path conditions automatically
   (the prototype's guard is trivially in-function; real
   candidates need the preceding checks captured as
   `__CPROVER_assume`).  This is the per-file-harness
   machinery extended with the CodeQL-identified source and
   guards.

## Next step

Build a CodeQL DB for **one fresh, less-swept subsystem**
(A+C), run a real tainted-size / tainted-index query (e.g.
the bundled `CWE-190/TaintedAllocationSize.ql` or a
kernel-source-model variant), and drive the surviving
candidates through the CBMC refinement.  The yield of that run
is the real test of the whole strategy.
